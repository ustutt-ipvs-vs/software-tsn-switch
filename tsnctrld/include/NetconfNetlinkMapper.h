#ifndef NETCONFNETLINKMAPPER_H
#define NETCONFNETLINKMAPPER_H
#include "TaprioModel.h"
#include "../../common/include/CncTypes.h"

class NetconfNetlinkMapper {
public:
	TaprioConfig mapToTaprio(const GclConfig_t& gcl);

private:
	uint64_t rationalToNs(const RationalTime_t& rationalTime) const;
	uint64_t ptpToNs(const PtpTime_t& time) const;
	//static TaprioConfig mapToTaprio(const GclConfig_t& gcl);
	static uint64_t toNs(const RationalTime_t& rationalTime);
	static uint64_t toNs(const PtpTime_t& time);
        static PtpTime_t fromNsToPtp(uint64_t Ns);
        static RationalTime_t fromNsToRational(uint64_t Ns);
};
#endif
