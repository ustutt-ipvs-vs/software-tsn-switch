#ifndef NETCONFNETLINKMAPPER_H
#define NETCONFNETLINKMAPPER_H
#include "TaprioModel.h"
#include "../../common/include/CncTypes.h"

class NetconfNetlinkMapper {
public:
	static TaprioConfig mapToTaprio(const GclConfig_t& gcl);
	static uint64_t toNs(const RationalTime_t& rationalTime);
	static uint64_t toNs(const PtpTime_t& time);
        static PtpTime_t fromNsToPtp(uint64_t Ns);
        static RationalTime_t fromNsToRational(uint64_t Ns);
};
#endif
