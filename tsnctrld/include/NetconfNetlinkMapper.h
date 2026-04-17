#ifndef NETCONFNETLINKMAPPER_H
#define NETCONFNETLINKMAPPER_H
#include "CncTypes.h"
#include "TaprioModel.h"

/**
 * @brief Helper class with methods to help building a @ref TaprioConfig from an @ref ietfInterface_t or an @ref
 * ietfInterface_t from a (partial) @ref TaprioConfig.
 */
class NetconfNetlinkMapper {
   public:
    static TaprioConfig mapToTaprio(const ietfInterface_t& iface);
    static PtpTime_t fromNsToPtp(int64_t Ns);
    static RationalTime_t fromNsToRational(int64_t Ns);

   private:
    static uint64_t rationalToNs(const RationalTime_t& rationalTime);
    static uint64_t ptpToNs(const PtpTime_t& time);
};
#endif
