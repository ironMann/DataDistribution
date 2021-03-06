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

#ifndef ALICEO2_STFBUILDER_DEVICE_H_
#define ALICEO2_STFBUILDER_DEVICE_H_

#include "StfBuilderInput.h"

#include <ReadoutDataModel.h>
#include <SubTimeFrameDataModel.h>
#include <SubTimeFrameFileSink.h>
#include <SubTimeFrameFileSource.h>
#include <ConcurrentQueue.h>
#include <Utilities.h>
#include <RootGui.h>

#include <TApplication.h>
#include <TCanvas.h>
#include <TH1.h>

#include <deque>
#include <memory>
#include <mutex>
#include <condition_variable>

#include <stdexcept>

#include <DataDistLogger.h>

namespace o2
{
namespace DataDistribution
{

enum StfBuilderPipeline {
  // only input stages
  eStfBuilderOut = 0,
  eStfFileSourceOut = 0,

  // input/output stages
  eStfFileSinkIn = 0,
  eStfFileSinkOut = 1,

  // output only stages
  eStfSendIn = 1,

  eStfNullIn = 2, // delete/drop
  eStfPipelineSize = 2,
  eStfInvalidStage = -1,
};

class StfBuilderDevice : public DataDistDevice,
                         public IFifoPipeline<std::unique_ptr<SubTimeFrame>>
{
 public:
  constexpr static int gStfOutputChanId = 0;

  static constexpr const char* OptionKeyInputChannelName = "input-channel-name";
  static constexpr const char* OptionKeyOutputChannelName = "output-channel-name";
  static constexpr const char* OptionKeyDplChannelName = "dpl-channel-name";
  static constexpr const char* OptionKeyStandalone = "stand-alone";
  static constexpr const char* OptionKeyMaxBufferedStfs = "max-buffered-stfs";
  static constexpr const char* OptionKeyGui = "gui";

  static constexpr const char* OptionKeyStfDetector = "detector";
  static constexpr const char* OptionKeyRdhSanityCheck = "rdh-data-check";
  static constexpr const char* OptionKeyFilterTriggerRdh4 = "rdh-filter-empty-trigger-v4";

  static bpo::options_description getDetectorProgramOptions();
  static bpo::options_description getStfBuildingProgramOptions();
  static o2::header::DataOrigin getDataOriginFromOption(const std::string pArg);

  /// Default constructor
  StfBuilderDevice();

  /// Default destructor
  ~StfBuilderDevice() override;

  void InitTask() final;
  void ResetTask() final;

  bool guiEnabled() const noexcept { return mBuildHistograms; }
  bool dplEnabled() const noexcept { return mDplEnabled; }
  bool isSandalone() const noexcept { return mStandalone; }

  const std::string& getInputChannelName() const { return mInputChannelName; }
  const std::string& getDplChannelName() const { return mDplChannelName; }

  auto& getOutputChannel() {
    if (isSandalone()) {
      return *mStandaloneChannel;
    }

    if (dplEnabled()) {
      return this->GetChannel(mDplChannelName);
    }

    return this->GetChannel(mOutputChannelName);
  }

 protected:
  void PreRun() final;
  void PostRun() final { };
  bool ConditionalRun() final;

  bool tryPopOldestStfs()
  {
    // try to drop one STF starting from back-end queues
    if (this->try_pop(eStfSendIn)) {
      return true;
    }

    if (this->try_pop(eStfFileSinkIn)) {
      return true;
    }

    return false;
  }

  unsigned getNextPipelineStage(unsigned pStage) final
  {
    StfBuilderPipeline lNextStage = eStfInvalidStage;

    switch (pStage) {
      case eStfBuilderOut:
      /* case eStfFileSourceOut: */
      {
        mNumStfs++;

        if (mPipelineLimit && (mNumStfs >= mMaxStfsInPipeline)) {

          // DROP policy in StfBuilder is to keep most current STFs. This will ensure that all
          // StfBuilders have the same set of STFs ready for distribution

          DDLOG(fair::Severity::WARNING) << "Dropping oldest STF due to reaching the maximum number of buffered "
                          "STFs in the process ("
                       << mMaxStfsInPipeline
                       << "). Consider increasing the limit, or reducing the input data rate.";

          if (tryPopOldestStfs()) {
            mNumStfs--;
          }
        }

        if (mFileSink.enabled()) {
          mNumStfs--;
          lNextStage = eStfFileSinkIn;
        } else {
          lNextStage = eStfSendIn;
        }
        break;
      }
      case eStfFileSinkOut:
      {
        mNumStfs++;
        lNextStage = eStfSendIn;
        break;
      }
      default:
        throw std::runtime_error("pipeline error");
    }

    if (!(lNextStage >= eStfFileSinkIn && lNextStage <= eStfNullIn)) {
      DDLOG(fair::Severity::ERROR) << "Stage error! next: " << lNextStage;
    }

    assert(lNextStage >= eStfFileSinkIn && lNextStage <= eStfNullIn);

    return lNextStage;
  }

  void StfOutputThread();

  /// config
  std::string mInputChannelName;
  std::string mOutputChannelName;
  std::string mDplChannelName;
  o2::header::DataOrigin mDataOrigin;
  bool mRdhSanityCheck = false;
  bool mRdh4FilterTrigger = false;
  bool mStandalone;
  bool mDplEnabled;
  std::int64_t mMaxStfsInPipeline;
  bool mPipelineLimit;

  /// Input Interface handler
  StfInputInterface mReadoutInterface;
  std::atomic_int64_t mNumStfs{ 0 };

  /// Internal threads
  std::thread mOutputThread;

  /// File sink
  SubTimeFrameFileSink mFileSink;

  /// File source
  std::unique_ptr<FairMQChannel> mStandaloneChannel;
  SubTimeFrameFileSource mFileSource;

  /// Root GUI stuff
  void GuiThread();
  bool mBuildHistograms = true;
  std::unique_ptr<RootGui> mGui;
  std::thread mGuiThread;

  RunningSamples<uint64_t> mStfSizeSamples;
  RunningSamples<float> mStfDataTimeSamples;
};

}
} /* namespace o2::DataDistribution */

#endif /* ALICEO2_STFBUILDER_DEVICE_H_ */
