#include "TestPluginExport.hpp"

extern "C" MYDB_TEST_PLUGIN_EXPORT int mydb_test_answer() {
    return 42;
}
