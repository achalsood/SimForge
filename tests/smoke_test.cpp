#include <simforge/core/engine.hpp>

#include <cstdlib>

int main()
{
    simforge::Engine engine;

    if (engine.initialized())
    {
        return EXIT_FAILURE;
    }

    engine.initialize();

    if (!engine.initialized())
    {
        return EXIT_FAILURE;
    }

    if (!engine.running())
    {
        return EXIT_FAILURE;
    }

    engine.begin_frame();
    engine.end_frame();

    if (engine.frame_count() != 1)
    {
        return EXIT_FAILURE;
    }

    engine.request_exit();

    if (engine.running())
    {
        return EXIT_FAILURE;
    }

    engine.shutdown();

    if (engine.initialized())
    {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}