## `letsplayd` - main server

-   public HTTP/3 endpoint for the web client
-   handles relaying video from emulators to users
    -   figure out something for CDN usage...
-   handles control in emulators yada yada
-   manages local runners (remote later!)

## Runners

-   letsplayd is a runner server.

    -   It will provide QUIC transport for runner clients to connect to
    -   For local runners, letsplayd will create a pipe and pass it to the runner process when starting an emulator (using command line arguments to tell it what fd the pipe is at)

-   `letsplay_runner_*` executables (runners) are runner clients
    -   They connect to a runner server, and the runner server tells them what to do
        -   Runner servers configure the runner before starting the game.
    -   Runner clients are in charge of encoding A/V data to send to the runner server
