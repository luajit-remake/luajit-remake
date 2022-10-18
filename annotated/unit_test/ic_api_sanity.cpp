#include "deegen_api.h"
#include "api_inline_cache.h"

extern "C" uint32_t testfn1(uint32_t key)
{
    ICHandler* ic = MakeInlineCache();
    ic->AddKey(key).SetImpossibleValue(123);
    return ic->Body([ic, key]() -> uint32_t {
        if (key < 100)
        {
            uint32_t key2 = key + 200;
            return ic->Effect([key, key2] {
                return key + key2;
            });
        }
        else
        {
            return ic->EffectValue(key - 50);
        }
    });
}
