// Provides the test runner entry point via Catch2's session API, so we only
// depend on the core Catch2 library (not the separate Catch2Main archive).
#include <catch2/catch_session.hpp>

int main(int argc, char* argv[])
{
    return Catch::Session().run(argc, argv);
}
