const char *route_plugin_introspect_string = "<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\" \"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n<node>\n    <interface name=\"org.nemomobile.Route.Manager\">\n        <method name=\"InterfaceVersion\">\n            <arg name=\"version\" type=\"u\" direction=\"out\"/>\n        </method>\n        <method name=\"GetAll\">\n            <arg name=\"output_device\" type=\"s\" direction=\"out\"/>\n            <arg name=\"output_device_mask\" type=\"u\" direction=\"out\"/>\n            <arg name=\"input_device\" type=\"s\" direction=\"out\"/>\n            <arg name=\"input_device_mask\" type=\"u\" direction=\"out\"/>\n            <arg name=\"features\" type=\"a(suu)\" direction=\"out\"/>\n        </method>\n        <method name=\"Enable\">\n            <arg name=\"feature\" type=\"s\" direction=\"in\"/>\n        </method>\n        <method name=\"Disable\">\n            <arg name=\"feature\" type=\"s\" direction=\"in\"/>\n        </method>\n        <signal name=\"AudioRouteChanged\">\n            <arg name=\"device\" type=\"s\"/>\n            <arg name=\"device_mask\" type=\"u\"/>\n        </signal>\n        <signal name=\"AudioFeatureChanged\">\n            <arg name=\"name\" type=\"s\"/>\n            <arg name=\"allowed\" type=\"u\"/>\n            <arg name=\"enabled\" type=\"u\"/>\n        </signal>\n    </interface>\n</node>\n";