#
# target: libbus1.so
#

libbus1_sources = [
        'peer.c',
        'node.c',
        'message.c',
        'bus1-peer.c',
]

libbus1_dependencies = [
        dep_crbtree,
        dep_csundry,
]


libbus1_private = static_library(
        'bus1-private',
        libbus1_sources,
        c_args: [
                '-fvisibility=hidden',
                '-fno-common',
        ],
        dependencies: libbus1_dependencies,
        pic: true,
)

libbus1_dep = declare_dependency(
        include_directories: include_directories('.'),
        link_with: libbus1_private,
        dependencies: [
                dep_crbtree,
                dep_csundry,
        ],
        version: meson.project_version(),
)

test_peer = executable('test-peer', ['test-peer.c'], dependencies: libbus1_dep)
test('Peer', test_peer)

#test_address = executable('test-address', ['dbus/test-address.c'], dependencies: libdbus_broker_dep)
#test('Address Handling', test_address)

#test_config = executable('test-config', ['launch/test-config.c', 'launch/config.c', 'launch/nss-cache.c'], dependencies: libdbus_broker_dep)
#test('Configuration Parser', test_config)

#test_dispatch = executable('test-dispatch', ['util/test-dispatch.c'], dependencies: libdbus_broker_dep)
#test('Event Dispatcher', test_dispatch)

#test_error = executable('test-error', ['util/test-error.c'], dependencies: libdbus_broker_dep)
#test('Error Handling', test_error)

#test_fdlist = executable('test-fdlist', ['util/test-fdlist.c'], dependencies: libdbus_broker_dep)
#test('Utility File-Desciptor Lists', test_fdlist)

#test_match = executable('test-match', ['bus/test-match.c'], dependencies: libdbus_broker_dep)
#test('D-Bus Match Handling', test_match)

#test_message = executable('test-message', ['dbus/test-message.c'], dependencies: libdbus_broker_dep)
#test('D-Bus Message Abstraction', test_message)

#test_name = executable('test-name', ['bus/test-name.c'], dependencies: libdbus_broker_dep)
#test('Name Registry', test_name)

#test_queue = executable('test-queue', ['dbus/test-queue.c'], dependencies: libdbus_broker_dep)
#test('D-Bus I/O Queues', test_queue)

#test_reply = executable('test-reply', ['bus/test-reply.c'], dependencies: libdbus_broker_dep)
#test('Reply Tracking', test_reply)

#test_sasl = executable('test-sasl', ['dbus/test-sasl.c'], dependencies: libdbus_broker_dep)
#test('D-Bus SASL Parser', test_sasl)

#test_socket = executable('test-socket', ['dbus/test-socket.c'], dependencies: libdbus_broker_dep)
#test('D-Bus Socket Abstraction', test_socket)

#test_stitching = executable('test-stitching', ['dbus/test-stitching.c'], dependencies: libdbus_broker_dep)
#test('Message Sender Stitching', test_stitching)

#test_user = executable('test-user', ['util/test-user.c'], dependencies: libdbus_broker_dep)
#test('User Accounting', test_user)
