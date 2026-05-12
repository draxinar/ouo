cmake_minimum_required(VERSION 3.19)

project(ouo_systemd)

install(
    FILES
        ouo.service
        ouo-backup.timer
        ouo-backup.service
        ouo-coredump@.service
        ouo-status.timer
        ouo-status.service
    DESTINATION /usr/lib/systemd/system
)
install(
    FILES ouo-backup.sh ouo-coredump.sh ouo-status.sh
    DESTINATION /opt/ouo
    PERMISSIONS
        OWNER_EXECUTE
        OWNER_WRITE
        OWNER_READ
        GROUP_READ
        GROUP_EXECUTE
        WORLD_READ
        WORLD_EXECUTE
)
install(FILES ouo.default DESTINATION /etc/default RENAME ouo)
install(FILES 50-ouo.rules DESTINATION /usr/share/polkit-1/rules.d)
