#include "../OptiScaler/dlssnr/NrBeforeSrContract.h"
#include <cassert>
#include <cstdio>

int main()
{
    using namespace DlssNr;
    auto extent = BeforeSrExtent({1536, 1632}, true, {3072, 3264}, {3072, 3264});
    assert(extent.width == 1536 && extent.height == 1632);
    extent = BeforeSrExtent({1536, 0}, true, {2048, 2176}, {3072, 3264});
    assert(extent.width == 2048 && extent.height == 2176);
    extent = BeforeSrExtent({1536, 1632}, false, {0, 2176}, {3072, 3264});
    assert(extent.width == 3072 && extent.height == 3264);
    assert(FitsBeforeSr({1536, 1632}, 3072, 3264));
    assert(!FitsBeforeSr({3072, 3264}, 1536, 1632));
    assert(!FitsBeforeSr({0, 1632}, 3072, 3264));
    assert(!FitsBeforeSr({1536, 0}, 3072, 3264));

    std::puts("PASS: pre-SR extent precedence and bounds");
}
