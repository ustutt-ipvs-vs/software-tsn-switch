#ifndef NETCONFNETLINKMAPPER_H
#define NETCONFNETLINKMAPPER_H
#include "../../common/include/CncTypes.h"
#include "TaprioModel.h"

class NetconfNetlinkMapper {
   public:
    static TaprioConfig mapToTaprio(const ietfInterface_t &iface);
    static PtpTime_t fromNsToPtp(uint64_t Ns);
    static RationalTime_t fromNsToRational(uint64_t Ns);

   private:
    static uint64_t rationalToNs(const RationalTime_t& rationalTime);
    static uint64_t ptpToNs(const PtpTime_t& time);
};
#endif
