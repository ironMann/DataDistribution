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

#include "SubTimeFrameFileSink.h"
#include "FilePathUtils.h"
#include "DataDistLogger.h"

#include <boost/algorithm/string/replace.hpp>
#include <boost/program_options/options_description.hpp>
#include <boost/filesystem.hpp>

#include <chrono>
#include <ctime>
#include <iostream>
#include <iomanip>

namespace o2
{
namespace DataDistribution
{

namespace bpo = boost::program_options;

////////////////////////////////////////////////////////////////////////////////
/// SubTimeFrameFileSink
////////////////////////////////////////////////////////////////////////////////

void SubTimeFrameFileSink::start()
{
  if (enabled())
    mSinkThread = std::thread(&SubTimeFrameFileSink::DataHandlerThread, this, 0);
}

void SubTimeFrameFileSink::stop()
{
  if (mSinkThread.joinable())
    mSinkThread.join();
}

bpo::options_description SubTimeFrameFileSink::getProgramOptions()
{
  bpo::options_description lSinkDesc("(Sub)TimeFrame file sink options", 120);

  lSinkDesc.add_options()(
    OptionKeyStfSinkEnable,
    bpo::bool_switch()->default_value(false),
    "Enable writing of (Sub)TimeFrames to file.")(
    OptionKeyStfSinkDir,
    bpo::value<std::string>()->default_value(""),
    "Specifies a destination directory where (Sub)TimeFrames are to be written. "
    "Note: A new directory will be created here for all output files.")(
    OptionKeyStfSinkFileName,
    bpo::value<std::string>()->default_value("%n"),
    "Specifies file name pattern: %n - file index, %D - date, %T - time.")(
    OptionKeyStfSinkStfsPerFile,
    bpo::value<std::uint64_t>()->default_value(0),
    "Specifies number of (Sub)TimeFrames per file. Default: 0 (unlimited)")(
    OptionKeyStfSinkFileSize,
    bpo::value<std::uint64_t>()->default_value(std::uint64_t(4) << 10), /* 4GiB */
    "Specifies target size for (Sub)TimeFrame files in MiB.")(
    OptionKeyStfSinkSidecar,
    bpo::bool_switch()->default_value(false),
    "Write a sidecar file for each (Sub)TimeFrame file containing information about data blocks "
    "written in the data file. "
    "Note: Useful for debugging. "
    "Warning: sidecar file format is not stable.");

  return lSinkDesc;
}


bool SubTimeFrameFileSink::loadVerifyConfig(const FairMQProgOptions& pFMQProgOpt)
{
  mEnabled = pFMQProgOpt.GetValue<bool>(OptionKeyStfSinkEnable);

  DDLOG(fair::Severity::INFO) << "(Sub)TimeFrame file sink is " << (mEnabled ? "enabled." : "disabled.");

  if (!mEnabled)
    return true;

  mRootDir = pFMQProgOpt.GetValue<std::string>(OptionKeyStfSinkDir);
  if (mRootDir.length() == 0) {
    DDLOG(fair::Severity::ERROR) << "(Sub)TimeFrame file sink directory must be specified";
    return false;
  }

  mFileNamePattern = pFMQProgOpt.GetValue<std::string>(OptionKeyStfSinkFileName);
  mStfsPerFile = pFMQProgOpt.GetValue<std::uint64_t>(OptionKeyStfSinkStfsPerFile);
  mFileSize = std::max(std::uint64_t(1), pFMQProgOpt.GetValue<std::uint64_t>(OptionKeyStfSinkFileSize));
  mFileSize <<= 20; /* in MiB */
  mSidecar = pFMQProgOpt.GetValue<bool>(OptionKeyStfSinkSidecar);

  // make sure directory exists and it is writable
  namespace bfs = boost::filesystem;
  bfs::path lDirPath(mRootDir);
  if (!bfs::is_directory(lDirPath)) {
    DDLOG(fair::Severity::ERROR) << "(Sub)TimeFrame file sink directory does not exist";
    return false;
  }

  // make a session directory
  mCurrentDir = (bfs::path(mRootDir) / FilePathUtils::getDataDirName(mRootDir)).string();
  if (!bfs::create_directory(mCurrentDir)) {
    DDLOG(fair::Severity::ERROR) << "Directory '" << mCurrentDir << "' for (Sub)TimeFrame file sink cannot be created";
    return false;
  }

  // print options
  DDLOG(fair::Severity::INFO) << "(Sub)TimeFrame Sink :: enabled       = " << (mEnabled ? "yes" : "no");
  DDLOG(fair::Severity::INFO) << "(Sub)TimeFrame Sink :: root dir      = " << mRootDir;
  DDLOG(fair::Severity::INFO) << "(Sub)TimeFrame Sink :: file pattern  = " << mFileNamePattern;
  DDLOG(fair::Severity::INFO) << "(Sub)TimeFrame Sink :: stfs per file = " << (mStfsPerFile > 0 ? std::to_string(mStfsPerFile) : "unlimited" );
  DDLOG(fair::Severity::INFO) << "(Sub)TimeFrame Sink :: max file size = " << mFileSize;
  DDLOG(fair::Severity::INFO) << "(Sub)TimeFrame Sink :: sidecar files = " << (mSidecar ? "yes" : "no");
  DDLOG(fair::Severity::INFO) << "(Sub)TimeFrame Sink :: write dir     = " << mCurrentDir;

  return true;
}

std::string SubTimeFrameFileSink::newStfFileName()
{
  time_t lNow;
  time(&lNow);
  char lTimeBuf[256];

  std::string lFileName = mFileNamePattern;

  std::stringstream lIdxString;
  lIdxString << std::dec << std::setw(8) << std::setfill('0') << mCurrentFileIdx;
  boost::replace_all(lFileName, "%n", lIdxString.str());

  strftime(lTimeBuf, sizeof(lTimeBuf), "%F", localtime(&lNow));
  boost::replace_all(lFileName, "%D", lTimeBuf);

  strftime(lTimeBuf, sizeof(lTimeBuf), "%H_%M_%S", localtime(&lNow));
  boost::replace_all(lFileName, "%T", lTimeBuf);

  mCurrentFileIdx++;
  return lFileName;
}

/// File writing thread
void SubTimeFrameFileSink::DataHandlerThread(const unsigned pIdx)
{
  std::uint64_t lCurrentFileSize = 0;
  std::uint64_t lCurrentFileStfs = 0;

  while (mDeviceI.IsRunningState()) {
    // Get the next STF
    std::unique_ptr<SubTimeFrame> lStf = mPipelineI.dequeue(mPipelineStageIn);
    if (!lStf) {
      // input queue is stopped, bail out
      break;
    }

    // make sure Stf is updated before writing
    lStf->updateStf();

    if (!enabled()) {
      DDLOG(fair::Severity::ERROR) << "Pipeline error, disabled file sing receiving STFs";
      break;
    }

    // check if we need a writer
    if (!mStfWriter) {
      namespace bfs = boost::filesystem;
      mStfWriter = std::make_unique<SubTimeFrameFileWriter>(
        bfs::path(mCurrentDir) / bfs::path(newStfFileName()), mSidecar);
    }

    // write
    if (mStfWriter->write(*lStf)) {
      lCurrentFileStfs++;
      lCurrentFileSize = mStfWriter->size();
    } else {
      mStfWriter.reset();
      mEnabled = false;
      DDLOG(fair::Severity::ERROR) << "(Sub)TimeFrame file sink: error while writing a file";
      DDLOG(fair::Severity::ERROR) << "(Sub)TimeFrame file sink: disabling writing";
    }

    // check if we should rotate the file
    if (((mStfsPerFile > 0) && (lCurrentFileStfs >= mStfsPerFile)) || (lCurrentFileSize >= mFileSize)) {
      lCurrentFileStfs = 0;
      lCurrentFileSize = 0;
      mStfWriter.reset(nullptr);
    }

    mPipelineI.queue(mPipelineStageOut, std::move(lStf));
  }
  DDLOG(fair::Severity::INFO) << "Exiting file sink thread[" << pIdx << "]...";
}
}
} /* o2::DataDistribution */
