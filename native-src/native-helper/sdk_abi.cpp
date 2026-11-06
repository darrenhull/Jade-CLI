// Compile against the user's installed SDK; no Jade headers are redistributed.
#include <cstddef>
#include <jomtypes.h>
static_assert(sizeof(Character)==2,"The installed Jade runtime requires the Unicode SDK ABI.");
static_assert(sizeof(void*)==8,"The adapter requires Windows x64.");
static_assert(sizeof(AccessType)==1 && sizeof(MethodInvocation)==1,"Unsupported Jade metadata enum ABI.");
static_assert(sizeof(DskObjectId)==22,"Unsupported Jade object identity ABI.");
static_assert(sizeof(ShortDskParam)==40,"Unsupported Jade parameter ABI.");
static_assert(sizeof(DskBuffer)==72,"Unsupported Jade buffer ABI.");
static_assert(offsetof(DskBuffer,oid)==50,"Unsupported Jade buffer identity offset.");
static_assert(offsetof(DskParamHeader,header)==0 && offsetof(DskHeader,format)==0,"Unsupported Jade parameter header.");
