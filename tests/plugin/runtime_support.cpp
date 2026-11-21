#include <Plugin/RuntimeSupport.h>

using SwiftSaveMenus::PackVersion;
using SwiftSaveMenus::SupportsRuntime;

static_assert(SupportsRuntime(PackVersion(1, 4, 15), PackVersion(2, 0, 12)));
static_assert(SupportsRuntime(PackVersion(1, 5, 97), PackVersion(2, 0, 20)));
static_assert(SupportsRuntime(PackVersion(1, 6, 640), PackVersion(2, 2, 3)));
static_assert(SupportsRuntime(PackVersion(1, 6, 1170), PackVersion(2, 2, 8)));
static_assert(SupportsRuntime(PackVersion(1, 6, 1179), PackVersion(2, 2, 6)));
static_assert(SupportsRuntime(PackVersion(1, 7, 104), PackVersion(2, 3, 1)));
static_assert(!SupportsRuntime(PackVersion(1, 4, 15), PackVersion(2, 0, 11)));
static_assert(!SupportsRuntime(PackVersion(1, 5, 97), PackVersion(1, 7, 3)));
static_assert(!SupportsRuntime(PackVersion(1, 7, 104), PackVersion(2, 3, 0)));
static_assert(!SupportsRuntime(PackVersion(1, 9, 32), PackVersion(1, 7, 3)));

int main() {}
