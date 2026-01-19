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
};
#endif
