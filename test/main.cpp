#include <CppUnitLite2.h>
#include <TestResultStdErr.h>

using namespace cppunitlite;

int main()
{
    TestResultStdErr result;
    TestRegistry::Instance().Run(result);
    return (result.FailureCount());
}
