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

#ifndef ALICEO2_SUBTIMEFRAME_DPL_H_
#define ALICEO2_SUBTIMEFRAME_DPL_H_

#include "SubTimeFrameDataModel.h"
#include "SubTimeFrameVisitors.h"

#include <Headers/DataHeader.h>

namespace o2
{
namespace DataDistribution
{

////////////////////////////////////////////////////////////////////////////////
/// StfDplAdapter
////////////////////////////////////////////////////////////////////////////////

class StfDplAdapter : public ISubTimeFrameVisitor
{
 public:
  StfDplAdapter() = delete;
  StfDplAdapter(FairMQChannel& pDplBridgeChan)
    : mChan(pDplBridgeChan)
  {
    mMessages.reserve(1024);
  }

  virtual ~StfDplAdapter() = default;

  void sendToDpl(std::unique_ptr<SubTimeFrame>&& pStf);

 protected:
  void visit(SubTimeFrame& pStf) override;

 private:
  std::vector<FairMQMessagePtr> mMessages;
  FairMQChannel& mChan;
};
}
} /* o2::DataDistribution */

#endif /* ALICEO2_SUBTIMEFRAME_DPL_H_ */
