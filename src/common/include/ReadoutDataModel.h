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

#ifndef ALICEO2_READOUT_DATAMODEL_H_
#define ALICEO2_READOUT_DATAMODEL_H_

#include <Headers/DataHeader.h>

#include <istream>
#include <cstdint>
#include <tuple>

namespace o2
{
namespace DataDistribution
{

////////////////////////////////////////////////////////////////////////////////
/// ReadoutSubTimeframeHeader
////////////////////////////////////////////////////////////////////////////////

// FIXME: copied from Readout/SubTimeframe.h
// definition of the header message for a subtimeframe made of 1
// message with this header followed by a message for each HBFrame
// All data belong to the same source (FEE link or user logic)

struct ReadoutSubTimeframeHeader {
  std::uint32_t mTimeFrameId; // id of timeframe
  std::uint32_t mNumberHbf; // number of HB frames (i.e. following messages)
  std::uint8_t mLinkId;       // common link id of all data in this HBframe
};

class ReadoutDataUtils {
public:

  static std::tuple<std::uint32_t,std::uint32_t,std::uint32_t>
  getSubSpecificationComponents(const char* pRdhData, const std::size_t len);

  static o2::header::DataHeader::SubSpecificationType getSubSpecification(const char* data, const std::size_t len);

  static std::tuple<uint32_t,uint32_t,uint32_t> getRdhNavigationVals(const char* pRdhData);

  enum SanityCheckMode {
    eNoSanityCheck,
    eSanityCheckDrop,
    eSanityCheckPrint

  };

  static bool rdhSanityCheck(const char* data, const std::size_t len);
  static bool filterTriggerEmpyBlocksV4(const char* pData, const std::size_t pLen);

public:
  static SanityCheckMode sRdhSanityCheckMode;
  static void setRdhSanityCheckMode(SanityCheckMode pMode) { sRdhSanityCheckMode = pMode; }
  static SanityCheckMode getRdhSanityCheckMode() { return sRdhSanityCheckMode; }
};

[[maybe_unused]] static
std::istream& operator>>(std::istream& in, ReadoutDataUtils::SanityCheckMode& pRetVal) {
  std::string token;
  in >> token;

  if (token == "off") {
    pRetVal = ReadoutDataUtils::eNoSanityCheck;
  } else if (token == "drop") {
    pRetVal = ReadoutDataUtils::eSanityCheckDrop;
  } else if (token == "print") {
    pRetVal = ReadoutDataUtils::eSanityCheckPrint;
  } else {
    in.setstate(std::ios_base::failbit);
  }
  return in;
}

}
} /* o2::DataDistribution */

#endif /* ALICEO2_READOUT_DATAMODEL_H_ */
