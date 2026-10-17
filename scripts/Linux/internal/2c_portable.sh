#!/bin/bash

shopt -s extglob

SCRIPTS_PATH="$(dirname "$(realpath "$0")")"/..
RESOURCES_PATH=$SCRIPTS_PATH/../../resources
INSTALL_PATH=$SCRIPTS_PATH/../../install
QT_DIR=""

#checking for parameters
for i in "$@"
do
case $i in
    -i=*|--install_path=*)
        INSTALL_PATH="${i#*=}"
        shift # past argument=value
        ;;
    -qt=*|--qt_dir=*)
        QT_DIR=${i#*=}
        shift # past argument=value
        ;;
    *)
        # unknown option
        ;;
esac
done

if [ ! -z "$QT_DIR" ]
then
    export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$QT_DIR/lib
    export QMAKE=$QT_DIR/bin/qmake
fi

ARCH=$(uname -m)

# Qt loads SSL dynamically, so linuxdeploy cannot discover it from DT_NEEDED.
# Use the SSL library beside the crypto dependency selected for this build.
CRYPTO_LIBRARY=$(ldd "$INSTALL_PATH/usr/bin/meshlab" | awk '$1 ~ /^libcrypto\.so/ && $2 == "=>" {print $3; exit}')
if [ -z "$CRYPTO_LIBRARY" ]; then
  CRYPTO_LIBRARY=$(readlink -f "$(pkg-config --variable=libdir libcrypto)/libcrypto.so")
fi
SSL_LIBRARY="$(dirname "$CRYPTO_LIBRARY")/$(basename "$CRYPTO_LIBRARY" | sed 's/^libcrypto/libssl/')"
if [ ! -f "$CRYPTO_LIBRARY" ] || [ ! -f "$SSL_LIBRARY" ]; then
  echo "Cannot find matching OpenSSL libraries: $CRYPTO_LIBRARY and $SSL_LIBRARY" >&2
  exit 1
fi

# Make sure that deploy succeeds before we start deleting files
if "$RESOURCES_PATH/linux/$ARCH/linuxdeploy" --appdir="$INSTALL_PATH" --plugin qt \
    --library="$CRYPTO_LIBRARY" --library="$SSL_LIBRARY"; then
  python3 "$SCRIPTS_PATH/internal/check_tls.py" "$INSTALL_PATH" || exit 1
  # after deploy, all required libraries are placed into usr/lib, therefore we can remove the ones in
  # usr/lib/meshlab (except for the ones that are loaded at runtime)
  cd $INSTALL_PATH/usr/lib/meshlab
  rm -v !("libIFXCore.so"|"libIFXExporting.so"|"libIFXScheduling.so")

  echo "$INSTALL_PATH is now a self contained meshlab application"

else
  echo "linuxdeploy failed with error code $?. Script was not completed successfully."
  exit 1
fi
