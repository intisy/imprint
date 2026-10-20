echo "data: $IMPRINT_DATA"
echo "personal: $IMPRINT_PERSONAL"
grep -q '"environment"' "$IMPRINT_SETUP" && echo "setup file holds the resolved setup"
cat "$IMPRINT_PERSONAL/tailscale.key"
