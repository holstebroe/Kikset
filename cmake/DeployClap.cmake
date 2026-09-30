# Script mode (cmake -DCLAP_FILE=<path> -P DeployClap.cmake): copies the .clap
# into the folder named by the CLAPTEST environment variable.
if(NOT CLAP_FILE)
    message(FATAL_ERROR "DeployClap: CLAP_FILE not set")
endif()
if(NOT DEFINED ENV{CLAPTEST})
    message(FATAL_ERROR "DeployClap: CLAPTEST environment variable is not set")
endif()
file(COPY "${CLAP_FILE}" DESTINATION "$ENV{CLAPTEST}")
message(STATUS "Deployed ${CLAP_FILE} to $ENV{CLAPTEST}")
