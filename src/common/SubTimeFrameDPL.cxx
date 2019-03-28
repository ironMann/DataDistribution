// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

#include "SubTimeFrameDPL.h"
#include <Framework/DataProcessingHeader.h>
#include <Headers/Stack.h>

namespace o2
{
namespace DataDistribution
{

using namespace o2::header;

////////////////////////////////////////////////////////////////////////////////
/// StfDplAdapter
////////////////////////////////////////////////////////////////////////////////

void StfDplAdapter::visit(SubTimeFrame& pStf)
{
  // Pack the Stf header
  o2::header::DataHeader lStfDistDataHeader(
    gDataDescSubTimeFrame,
    o2::header::gDataOriginFLP,
    0, // TODO: subspecification? FLP ID? EPN ID?
    sizeof(SubTimeFrame::Header)
  );
  lStfDistDataHeader.payloadSerializationMethod = gSerializationMethodNone;

  o2::framework::DataProcessingHeader lDplHeader(
    pStf.header().mId, /* Stf ID */
    0
  );

  auto lHdrStack = Stack(lStfDistDataHeader, lDplHeader);

  auto lDataHeaderMsg = mChan.NewMessage(lHdrStack.size());
  if (!lDataHeaderMsg) {
    LOG(ERROR) << "Allocation error: Stf DataHeader::size: " << sizeof(DataHeader);
    throw std::bad_alloc();
  }

  std::memcpy(lDataHeaderMsg->GetData(), lHdrStack.data(), lHdrStack.size());

  auto lDataMsg = mChan.NewMessage(sizeof(SubTimeFrame::Header));
  if (!lDataMsg) {
    LOG(ERROR) << "Allocation error: Stf::Header::size: " << sizeof(SubTimeFrame::Header);
    throw std::bad_alloc();
  }
  std::memcpy(lDataMsg->GetData(), &pStf.header(), sizeof(SubTimeFrame::Header));

  mMessages.emplace_back(std::move(lDataHeaderMsg));
  mMessages.emplace_back(std::move(lDataMsg));

  for (auto& lDataIdentMapIter : pStf.mData) {
    for (auto& lSubSpecMapIter : lDataIdentMapIter.second) {

      auto & lHBFrameVector = lSubSpecMapIter.second;

      for (uint64_t i = 0; lHBFrameVector.size(); i++) {

        // FIXME: update the subspec field to include the element ID for DPL
        //
        // DPL tuple (origin, description, data subspecification, timestamp)
        //
        // O2 messages belonging to a single STF:
        //  - timestamp is always STF ID
        //  - (origin, description) can repeat in multiple messages
        //  - (origin, description, subspecification & 0xFFFFFFFF00000000) can also repeat
        //  - (origin, description, subspecification) is unique
        //

        DataHeader lDataHdr;
        memcpy(&lDataHdr, lHBFrameVector[i].mHeader->GetData(), sizeof(DataHeader));

        lDataHdr.subSpecification = (lDataHdr.subSpecification << 32) | i;

        memcpy(lHBFrameVector[i].mHeader->GetData(), &lDataHdr, sizeof(DataHeader));

        mMessages.emplace_back(std::move(lHBFrameVector[i].mHeader));
        mMessages.emplace_back(std::move(lHBFrameVector[i].mData));
      }
    }
  }

  pStf.mData.clear();
  pStf.mHeader = SubTimeFrame::Header();
}

void StfDplAdapter::sendToDpl(std::unique_ptr<SubTimeFrame>&& pStf)
{
  mMessages.clear();
  pStf->accept(*this);

  mChan.Send(mMessages);

  // make sure headers and chunk pointers don't linger
  mMessages.clear();
}
}
} /* o2::DataDistribution */
