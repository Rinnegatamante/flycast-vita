/* Keep a link-content identity in a loaded segment, accessible to the cache. */
SECTIONS
{
  .note.gnu.build-id : ALIGN(4)
  {
    mvc2_native_build_id = .;
    KEEP(*(.note.gnu.build-id))
  }
}
INSERT AFTER .rodata;
