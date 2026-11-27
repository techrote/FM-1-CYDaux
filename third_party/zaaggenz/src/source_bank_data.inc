/* CYDaux native-only build: no reference-derived source-image assets.
 * This explicit empty bank is local build glue, not an upstream asset.
 * The adapter enforces ZG_NATIVE for every patch. */
const unsigned zg_source_count = 0;
const zg_source_info zg_source_bank[1] = {{0}};
const int16_t zg_source_decode_table[256] = {0};
