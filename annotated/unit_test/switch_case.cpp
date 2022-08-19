#include "deegen_api.h"

using namespace DeegenAPI;

void testfn(int a, int b, int c)
{
    SwitchOnMutuallyExclusiveCases
    {
        CASE(a % 3 == 0 && b % 4 == 0)
        {
            int result = a * b + c;
            Return(TValue::CreateDouble(result));
        },
        CASE(a % 3 == 1 && b % 4 == 2)
        {
            int result = b * c;
            Return(TValue::CreateDouble(result));
        },
        DEFAULT()
        {
            Return(TValue::Nil());
        }
    };
}
