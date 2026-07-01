#!/bin/sh
set -eu

echo "$(pwd)"

add_plugin_rpath() {
    plugin_path="$1"
    if [ -f "$plugin_path" ]; then
        if ! otool -l "$plugin_path" | grep -q '@loader_path/../../Frameworks'; then
            install_name_tool -add_rpath @loader_path/../../Frameworks "$plugin_path"
        fi
    else
        echo "Skipping missing Qt plugin: $plugin_path"
    fi
}

for plugin_path in \
    ./bin/Sigil.app/Contents/PlugIns/iconengines/libqsvgicon.dylib \
    ./bin/Sigil.app/Contents/PlugIns/imageformats/libqgif.dylib \
    ./bin/Sigil.app/Contents/PlugIns/imageformats/libqicns.dylib \
    ./bin/Sigil.app/Contents/PlugIns/imageformats/libqico.dylib \
    ./bin/Sigil.app/Contents/PlugIns/imageformats/libqjpeg.dylib \
    ./bin/Sigil.app/Contents/PlugIns/imageformats/libqmacheif.dylib \
    ./bin/Sigil.app/Contents/PlugIns/imageformats/libqmacjp2.dylib \
    ./bin/Sigil.app/Contents/PlugIns/imageformats/libqpdf.dylib \
    ./bin/Sigil.app/Contents/PlugIns/imageformats/libqsvg.dylib \
    ./bin/Sigil.app/Contents/PlugIns/imageformats/libqtga.dylib \
    ./bin/Sigil.app/Contents/PlugIns/imageformats/libqtiff.dylib \
    ./bin/Sigil.app/Contents/PlugIns/imageformats/libqwbmp.dylib \
    ./bin/Sigil.app/Contents/PlugIns/imageformats/libqwebp.dylib \
    ./bin/Sigil.app/Contents/PlugIns/networkinformation/libqscnetworkreachability.dylib \
    ./bin/Sigil.app/Contents/PlugIns/platforminputcontexts/libqtvirtualkeyboardplugin.dylib \
    ./bin/Sigil.app/Contents/PlugIns/platforms/libqcocoa.dylib \
    ./bin/Sigil.app/Contents/PlugIns/position/libqtposition_cl.dylib \
    ./bin/Sigil.app/Contents/PlugIns/position/libqtposition_nmea.dylib \
    ./bin/Sigil.app/Contents/PlugIns/position/libqtposition_positionpoll.dylib \
    ./bin/Sigil.app/Contents/PlugIns/styles/libqmacstyle.dylib \
    ./bin/Sigil.app/Contents/PlugIns/tls/libqcertonlybackend.dylib \
    ./bin/Sigil.app/Contents/PlugIns/tls/libqopensslbackend.dylib \
    ./bin/Sigil.app/Contents/PlugIns/tls/libqsecuretransportbackend.dylib
do
    add_plugin_rpath "$plugin_path"
done
