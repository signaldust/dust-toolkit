#!/bin/bash

if [ "$#" != "2" ]; then
    echo "Usage:"
    echo "   $0 <bundle-name> <binary-name>"
    echo "Supported environment variables:"
    echo "  BUNDLE_VENDOR_DNS   - reverse DNS for bundle ID, default: org.example"
    exit 1
fi

BUNDLE_NAME=$1
BINARY_NAME=$2

# build the binary if it's not there yet
make -q $BINARY_NAME

# check if we want an extension other than .component
# Environment variables ..
if [ -z "$BUNDLE_VENDOR_DNS" ]; then
    BUNDLE_VENDOR_DNS=org.example
fi

# build the output path, this is just bundle/Contents
OUTPATH=$BUNDLE_NAME/Contents

# remake the application bundle directory
echo "BUNDLE $BUNDLE_NAME"
rm -rf $OUTPATH
mkdir -p $OUTPATH/MacOS # && echo -n "BNDL????" > $OUTPATH/PkgInfo
# use /usr/bin explicitly 'cos GNU strip (eg. from homebrew) is broken)
if [ -z "BUNDLE_NO_STRIP" ]; then
    cp -a $BINARY_NAME $OUTPATH/MacOS/$(basename $BINARY_NAME)
else
    /usr/bin/strip -Sx $BINARY_NAME -o $OUTPATH/MacOS/$(basename $BINARY_NAME)
fi

# finally we need to generate an Info.plist
#echo "Creating Info.plist for $PLUGNAME..."
(sed -e "s/BUNDLE_NAME/${BUNDLE_NAME%%.*}/g" | \
sed -e "s/BINARY_NAME/$(basename $BINARY_NAME)/g" | \
sed -e "s/BUNDLE_INFO/$BUNDLE_INFO/g" | \
sed -e "s/VENDOR_DNS/$BUNDLE_VENDOR_DNS/g" > $OUTPATH/Info.plist) <<END_TEMPLATE
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
    "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key>
    <string>English</string>
    <key>CFBundleExecutable</key>
    <string>BINARY_NAME</string>
    <key>CFBundleGetInfoString</key>
    <string>BUNDLE_INFO</string>
    <key>CFBundleIconFile</key>
    <string></string>
    <key>CFBundleIdentifier</key>
    <string>VENDOR_DNS.BUNDLE_NAME</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>CFBundleName</key>
    <string>BUNDLE_NAME</string>
    <key>CFBundlePackageType</key>
    <string>BNDL</string>
    <key>CFBundleSignature</key>
    <string>????</string>
    <key>CFBundleVersion</key>
    <string>1.0</string>
</dict>
</plist>
END_TEMPLATE
