cmake_minimum_required(VERSION 3.19)

project(
    ouo
    SPDX_LICENSE MIT
    HOMEPAGE_URL https://github.com/draxinar/ouo
    LANGUAGES C
)

# List of optional flags
option(OUO_32BIT OFF)
option(OUO_ASAN OFF)
option(OUO_VALGRIND OFF)
option(OUO_WERROR OFF)
option(OUO_SYSTEMD OFF)

add_executable(${PROJECT_NAME})

# Explicitly set C99 standard
set_property(TARGET ${PROJECT_NAME} PROPERTY CMAKE_C_STANDARD 99)

# Explicitly set which warnings to emit
target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)

# Use git as source for version string
find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${PROJECT_SOURCE_DIR}/.git")
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --always --dirty
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE GIT_VERSION
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
else()
    set(GIT_VERSION "unknown")
endif()

target_compile_definitions(
    ${PROJECT_NAME}
    PRIVATE
        # Conform to POSIX.1-2008 base specification
        _POSIX_C_SOURCE=200809L
        # Version string
        OUO_VERSION="${GIT_VERSION}"
)

#
# Dependencies
#

# Find thread library on macOS/Windows
set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads)
target_link_libraries(${PROJECT_NAME} PRIVATE Threads::Threads)

# Find math library on *unix
find_library(MATH_LIBRARY m)
if(MATH_LIBRARY)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${MATH_LIBRARY})
endif()

#
# Sources
#

target_sources(
    ${PROJECT_NAME}
    PRIVATE
        account.c
        account.h
        anim.c
        anim.h
        bankdefs.c
        bankdefs.h
        bboard.c
        bboard.h
        blockmanager.c
        blockmanager.h
        blowfish.c
        blowfish.h
        book.c
        book.h
        channel.c
        channel.h
        combat.c
        combat.h
        config.c
        config.h
        container.c
        container.h
        containerhandle.c
        containerhandle.h
        convo.c
        convo.h
        corpse.c
        corpse.h
        cstring.c
        cstring.h
        dat.h
        defcon.c
        defcon.h
        dice.c
        dice.h
        dynamic.c
        dynamic.h
        egg.c
        egg.h
        entity.c
        entity.h
        entitymanager.c
        entitymanager.h
        entitymap.c
        entitymap.h
        feature.c
        feature.h
        feistel.c
        feistel.h
        filemanager.c
        filemanager.h
        fns.c
        fns.h
        gamecentmon.c
        gamecentmon.h
        gmedit.c
        gmedit.h
        gm_names.h
        help_queue.c
        help_queue.h
        huffman.c
        huffman.h
        io.c
        io.h
        item.c
        item.h
        list.c
        listensocket.c
        listensocket.h
        list.h
        load.c
        load.h
        location.c
        location.h
        log.c
        log.h
        magicfactory.c
        magicfactory.h
        magiclist.c
        magiclist.h
        main.c
        main.h
        mobile.c
        mobile.h
        multi.c
        multi.h
        nodepool.c
        nodepool.h
        npc.c
        npc.h
        objvar.c
        objvar.h
        packet_handler.c
        packet_handler.h
        packet_manager.c
        packet_manager.h
        packet_utils.c
        packet_utils.h
        pending_auth.c
        pending_auth.h
        player.c
        player.h
        random.c
        random.h
        region.c
        region.h
        resbank.c
        resbank.h
        res.c
        res.h
        resource_entity.c
        resource_entity.h
        resource_regrowth.c
        resource_regrowth.h
        resquery.c
        resquery.h
        scommand.c
        scommand.h
        serial_list.c
        serial_list.h
        sha256.c
        sha256.h
        shopkeeper.c
        shopkeeper.h
        signpost.c
        signpost.h
        skill.c
        skill.h
        socket.c
        socket.h
        stddeque.c
        stddeque.h
        stdlist.c
        stdlist.h
        stdptrlist.c
        stdptrlist.h
        stl.c
        stl.h
        streambuf.c
        streambuf.h
        taglist.c
        taglist.h
        template.c
        template.h
        terrain.c
        terrain.h
        time.c
        time.h
        timer.c
        timer.h
        trade.c
        trade.h
        twofish.c
        twofish.h
        usersock.c
        usersock.h
        ustring.c
        ustring.h
        utils.c
        utils.h
        version.c
        version.h
        vg_pool.h
        vtable.c
        vtable.h
        watchdog.c
        watchdog.h
        weapon.c
        weapon.h
        weather.c
        weather.h
        wombat_builtins.inc
        wombat.c
        wombat_compile.c
        wombat_compile.h
        wombat_escript.c
        wombat_escript.h
        wombat_exec.c
        wombat_exec.h
        wombat.h
        wombat_stl.c
        wombat_stl.h
        world.c
        world.h
)

#
# Option flags
#

if(OUO_32BIT)
    target_link_options(${PROJECT_NAME} PRIVATE -m32)
    target_compile_options(${PROJECT_NAME} PRIVATE -m32)
endif()
if(OUO_ASAN)
    target_link_options(${PROJECT_NAME} PRIVATE -fsanitize=address)
    target_compile_options(${PROJECT_NAME} PRIVATE -fsanitize=address)
endif()
if(OUO_VALGRIND)
    target_compile_definitions(${PROJECT_NAME} PRIVATE VALGRIND=1)
endif()
if(OUO_WERROR)
    target_compile_options(${PROJECT_NAME} PRIVATE -Werror)
endif()
if(OUO_SYSTEMD)
    add_subdirectory(systemd)
endif()

#
# Install
#

install(TARGETS ${PROJECT_NAME} DESTINATION /opt/ouo/run)
