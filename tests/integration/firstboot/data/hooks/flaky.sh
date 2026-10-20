if [ ! -f /var/tmp/allow-success ]; then
    echo "flaky: failing on purpose"
    exit 3
fi
echo "stage=$IMPRINT_STAGE key=$(cat "$IMPRINT_PERSONAL/tailscale.key")" > /var/tmp/flaky-succeeded
grep -q '"boot-test"' "$IMPRINT_SETUP"
