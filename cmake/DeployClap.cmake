# Copies the built .clap into $CLAPTEST (if set) after each build, as Acidus does.
function(kikset_deploy_clap TARGET)
    if(DEFINED ENV{CLAPTEST})
        add_custom_command(TARGET ${TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${TARGET}> "$ENV{CLAPTEST}/"
            COMMENT "Deploying ${TARGET} to $ENV{CLAPTEST}")
    endif()
endfunction()
