/* Document/renderer unit checks. GUI regressions live in paste_corruption_repro.c. */
#define wWinMain text_wWinMain
#include "main.c"
#undef wWinMain

int main(void)
{
  const char input[] = "A\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80\tZ\n";
  const uint32_t expected[] = {'A', 0xe9, 0x20ac, 0x1f600, ' ', ' ', 'Z'};
  const u64 boundaries[] = {0, 1, 3, 6, 10, 11, 12, 13};
  const unsigned char invalid[][4] = {
    {0xc0, 0xaf}, {0xe0, 0x80, 0x80}, {0xed, 0xa0, 0x80},
    {0xf4, 0x90, 0x80, 0x80}, {0xf5, 0x80, 0x80, 0x80}, {0x80}, {0xe2, 'X'}
  };
  Document doc;
  uint32_t cells[16], cp;
  int failed = 0;
  memset(&doc, 0, sizeof(doc));
  doc_set_empty(&doc);
  /* Leave physical gaps in ADD backing so every UTF-8 byte crosses a piece. */
  for (size_t i = 0; i < sizeof(input) - 1; ++i)
  {
    char pair[2] = {'!', input[i]};
    u64 off = doc.len;
    if (!doc_insert_bytes(&doc, off, pair, 2, NULL) || !doc_delete_range(&doc, off, 1)) return 2;
  }
  if (doc.piece_count != sizeof(input) - 1) failed++;
  for (unsigned i = 0; i + 1 < _countof(boundaries); ++i)
  {
    if (doc_next_character(&doc, boundaries[i]) != boundaries[i + 1]) failed++;
    if (doc_previous_character(&doc, boundaries[i + 1]) != boundaries[i]) failed++;
  }
  if (doc_line_visual_width(&doc, 0, doc.len) != _countof(expected)) failed++;
  if (line_to_wide_visible(&doc, 0, doc.len, 0, 16, cells) != _countof(expected) ||
      memcmp(cells, expected, sizeof(expected))) failed++;
  for (unsigned col = 0; col < _countof(expected); ++col)
  {
    if (line_to_wide_visible(&doc, 0, doc.len, col, 1, cells) != 1 || cells[0] != expected[col]) failed++;
  }
  for (unsigned col = 0; col <= 4; ++col)
    if (doc_line_byte_col_from_visual_col(&doc, 0, col) != boundaries[col]) failed++;
  for (unsigned i = 0; i < _countof(invalid); ++i)
    if (utf8_decode(invalid[i], 4, &cp) != 1 || cp != 0xfffd) failed++;
  if (utf8_decode((const unsigned char *)"\xf0\x9f\x98", 3, &cp) != 1 || cp != 0xfffd) failed++;
  doc_clear(&doc);
  printf("%s UTF-8 decoding, piece boundaries, viewport clipping and invalid input\n", failed ? "FAIL" : "PASS");
  return failed ? 1 : 0;
}
