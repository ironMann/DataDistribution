// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include "StfSenderDevice.h"

#include <ConfigConsul.h>

#include <SubTimeFrameDataModel.h>
#include <SubTimeFrameVisitors.h>

#include <options/FairMQProgOptions.h>

#include <chrono>
#include <thread>

namespace o2
{
namespace DataDistribution
{

using namespace std::chrono_literals;

StfSenderDevice::StfSenderDevice()
  : DataDistDevice(),
    IFifoPipeline(ePipelineSize),
    mFileSink(*this, *this, eFileSinkIn, eFileSinkOut),
    mOutputHandler(*this),
    mRpcServer(mOutputHandler)
{
}

StfSenderDevice::~StfSenderDevice()
{
}

void StfSenderDevice::InitTask()
{
  DataDistLogger::SetThreadName("stfs-main");

  mInputChannelName = GetConfig()->GetValue<std::string>(OptionKeyInputChannelName);
  mStandalone = GetConfig()->GetValue<bool>(OptionKeyStandalone);
  mMaxStfsInPipeline = GetConfig()->GetValue<std::int64_t>(OptionKeyMaxBufferedStfs);
  mBuildHistograms = GetConfig()->GetValue<bool>(OptionKeyGui);

  // Discovery
  mDiscoveryConfig = std::make_shared<ConsulStfSender>(ProcessType::StfSender, Config::getEndpointOption(*GetConfig()));

  auto &lStatus = mDiscoveryConfig->status();
  lStatus.mutable_info()->set_type(StfSender);
  lStatus.mutable_info()->set_process_id(Config::getIdOption(*GetConfig()));
  lStatus.mutable_info()->set_ip_address(Config::getNetworkIfAddressOption(*GetConfig()));

  lStatus.mutable_partition()->set_partition_id(Config::getPartitionOption(*GetConfig()));

  mDiscoveryConfig->write();

  // Buffering limitation
  if (mMaxStfsInPipeline > 0) {
    if (mMaxStfsInPipeline < 4) {
      mMaxStfsInPipeline = 4;
      DDLOG(fair::Severity::INFO) << "Max buffered SubTimeFrames limit increased to: " << mMaxStfsInPipeline;
    }
    mPipelineLimit = true;
    DDLOG(fair::Severity::WARN) << "Max buffered SubTimeFrames limit is set to " << mMaxStfsInPipeline
              << ". Consider increasing it if data loss occurs.";
  } else {
    mPipelineLimit = false;
    DDLOG(fair::Severity::INFO) << "Not imposing limits on number of buffered SubTimeFrames. "
                 "Possibility of creating back-pressure";
  }

  // File sink
  if (!mFileSink.loadVerifyConfig(*GetConfig()))
    exit(-1);

  // check if any outputs enabled
  if (mStandalone && !mFileSink.enabled()) {
    DDLOG(fair::Severity::WARNING) << "Running in standalone mode and with STF file sink disabled. "
                    "Data will be lost.";
  }

  // gui thread
  if (mBuildHistograms) {
    mGui = std::make_unique<RootGui>("STFSender", "STF Sender", 400, 400);
    mGui->Canvas().Divide(1, 1);
    mGuiThread = std::thread(&StfSenderDevice::GuiThread, this);
  }
}

void StfSenderDevice::PreRun()
{
  while (!mTfSchedulerRpcClient.start(mDiscoveryConfig)) {

    // try to reach the scheduler unless we should exit
    if (IsRunningState() && NewStatePending()) {
      return;
    }

    std::this_thread::sleep_for(250ms);
  }

  // Start output handler
  // NOTE: required even in standalone operation
  mOutputHandler.start(mDiscoveryConfig);

  // start the RPC server after output
  int lRpcRealPort = 0;
  auto &lStatus = mDiscoveryConfig->status();
  mRpcServer.start(lStatus.info().ip_address(), lRpcRealPort);
  lStatus.set_rpc_endpoint(lStatus.info().ip_address() + ":" + std::to_string(lRpcRealPort));
  mDiscoveryConfig->write();

  // start file sink
  if (mFileSink.enabled()) {
    mFileSink.start();
  }
  // start the receiver thread
  mReceiverThread = std::thread(&StfSenderDevice::StfReceiverThread, this);
}

void StfSenderDevice::ResetTask()
{
  // Stop the pipeline
  stopPipeline();

  // stop the receiver thread
  if (mReceiverThread.joinable()) {
    mReceiverThread.join();
  }

  // stop file sink
  if (mFileSink.enabled()) {
    mFileSink.stop();
  }

  // start the RPC server after output
  mRpcServer.stop();

  // Stop output handler
  mOutputHandler.stop();

  // Stop the Scheduler RPC client
  mTfSchedulerRpcClient.stop();

  // wait for the gui thread
  if (mBuildHistograms && mGuiThread.joinable()) {
    mGuiThread.join();
  }

  DDLOG(fair::Severity::INFO) << "ResetTask() done... ";
}

void StfSenderDevice::StfReceiverThread()
{
  auto& lInputChan = GetChannel(mInputChannelName, 0);

  InterleavedHdrDataDeserializer lStfReceiver;
  std::unique_ptr<SubTimeFrame> lStf;

  // wait for the device to go into RUNNING state
  WaitForRunningState();

  while (IsRunningState()) {
    lStf = lStfReceiver.deserialize(lInputChan);
    if (!lStf) {
      std::this_thread::sleep_for(20ms);
      continue; // timeout
    }

    const TimeFrameIdType lStfId = lStf->header().mId;

    { // rate-limited LOG: print stats every 100 TFs
      static unsigned long floodgate = 0;
      if (floodgate++ % 100 == 0) {
        DDLOG(fair::Severity::DEBUG) << "TF[" << lStfId << "] size: " << lStf->getDataSize();
      }
    }

    queue(eReceiverOut, std::move(lStf));
  }

  DDLOG(fair::Severity::INFO) << "Exiting StfOutputThread...";
}

void StfSenderDevice::GuiThread()
{
  std::unique_ptr<TH1S> lStfPipelinedCntHist = std::make_unique<TH1S>("StfQueuedH", "Queued STFs", 150, -0.5, 150 - 0.5);
  lStfPipelinedCntHist->GetXaxis()->SetTitle("Number of queued Stf");

  // wait for the device to go into RUNNING state
  WaitForRunningState();

  while (IsRunningState()) {
    DDLOG(fair::Severity::INFO) << "Updating histograms...";

    mGui->Canvas().cd(1);
    mGui->DrawHist(lStfPipelinedCntHist.get(), this->getPipelinedSizeSamples());

    mGui->Canvas().Modified();
    mGui->Canvas().Update();

    DDLOG(fair::Severity::INFO) << "* Queued STFs in StfSender: " << this->getPipelineSize();

    std::this_thread::sleep_for(5s);
  }
  DDLOG(fair::Severity::INFO) << "Exiting GUI thread...";
}

bool StfSenderDevice::ConditionalRun()
{
  // nothing to do here sleep for awhile
  std::this_thread::sleep_for(1000ms);
  return true;
}
}
} /* namespace o2::DataDistribution */
