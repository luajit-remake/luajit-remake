#include "deegen_api.h"
#include "api_inline_cache.h"

extern "C" uint32_t testfn1(uint32_t key, uint32_t k2)
{
    ICHandler* ic = MakeInlineCache();
    ic->AddKey(key).SpecifyImpossibleValue(123);
    return ic->Body([ic, key, k2]() -> uint32_t {
        if (key < 100)
        {
            uint32_t k3 = k2 + 200;
            return ic->Effect([key, k3, k2] {
                IcSpecifyCaptureValueRange(k3, 0, 1000);
                return key + k3 + k2;
            });
        }
        else
        {
            return ic->Effect([key] {
                return key;
            });
        }
    });
}
