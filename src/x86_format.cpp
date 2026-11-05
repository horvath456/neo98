// SPDX-License-Identifier: GPL-2.0-or-later
// ANSI FormatMessage service. The source selection, message lookup and buffer
// decisions follow win98_format_message.h; the insert engine below is new code.
#ifdef NEO98_RUNTIME_CONTENT
#include "windows_runtime_data.h"
#else
#include "../build/generated/win98_message_table.h"
#endif
#include "win98_format_message.h"
#include "x86_app.h"
#include <stdio.h>
#include <string.h>
namespace X86 {
// Copies the message text of one identifier out of a table resource. Returns
// false when the identifier is absent.
static bool MessageText(const unsigned char *table, unsigned size, unsigned identifier,
                        char *out, unsigned capacity) {
  Win98FormatMessage::Entry entry;
  if (!Win98FormatMessage::Lookup(table, size, identifier, entry))
    return false;
  unsigned written = 0;
  if (entry.wide) {
    // a Unicode entry is converted to the caller's code page.
    for (unsigned i = 0; i + 1 < entry.bytes && written + 1 < capacity; i += 2) {
      const unsigned value = entry.text[i] | unsigned(entry.text[i + 1]) << 8;
      if (!value)
        break;
      out[written++] = value < 256 ? char(value) : '?';
    }
  } else {
    while (written < entry.bytes && written + 1 < capacity && entry.text[written]) {
      out[written] = char(entry.text[written]);
      ++written;
    }
  }
  out[written] = 0;
  return true;
}
// The message table of one mapped module, for FORMAT_MESSAGE_FROM_HMODULE.
// The lookup uses resource type 11, name 1 and the caller's language. It is
// not the high word of the identifier plus one that NT uses: Win98 always
// reads resource 11/1, and the full identifier selects the block. The bytes
// stand in the mapped image, so the table of every module the loader holds is
// reachable. A handle that is not a module, an image without that resource and
// a table without the identifier all answer false; the caller decides whether
// the system table follows.
bool Application::ModuleMessage(unsigned module, unsigned identifier, unsigned language, char *out,
                                unsigned capacity) {
  // A null module is the process's own image, as for GetModuleHandle(NULL);
  // PING.EXE and TRACERT.EXE keep their texts in their own message tables.
  const unsigned index = module ? ModuleIndex(module) : FindModule(nullptr);
  const auto *image = index < ModuleCapacity ? modules[index].pe : nullptr;
  if (!image)
    return false;
  const Pe32::Image::ResourceKey type{11, nullptr, 0}, id{1, nullptr, 0};
  Pe32::Image::ResourceEntry resource;
  unsigned error = 0;
  // The same neutral-language fallback FindResourceA takes: English, then the
  // first language present.
  if (!image->FindResource(type, id, language, resource, &error) &&
      !(error == 1815 && (image->FindResource(type, id, 0x409, resource, &error) ||
                          image->FindResource(type, id, ~0u, resource, &error))))
    return false;
  // A table above one mebibyte is not a Win98 message table; refuse it rather
  // than reading an arbitrary length out of the guest.
  if (!resource.size || resource.size > (1u << 20))
    return false;
  // Plain new: this runtime is freestanding and without exceptions, so a
  // failed allocation answers null, the way the rest of src/ treats it.
  auto *bytes = new unsigned char[resource.size];
  if (!bytes)
    return false;
  Read(image->Base() + resource.data, bytes, resource.size);
  const bool found = MessageText(bytes, resource.size, identifier, out, capacity);
  delete[] bytes;
  return found;
}
unsigned Application::FormatMessage(const unsigned *a) {
  const unsigned width = Win98FormatMessage::Width(a[0]);
  // The documented flag set: width mask, allocate, ignore inserts and the
  // three sources plus the argument array.
  constexpr unsigned known = Win98FormatMessage::WidthMask | Win98FormatMessage::AllocateBuffer |
                             Win98FormatMessage::IgnoreInserts | Win98FormatMessage::FromString |
                             Win98FormatMessage::FromModule | Win98FormatMessage::FromSystem |
                             Win98FormatMessage::ArgumentArray;
  if (a[0] & ~known) {
    Fail("Unsupported FormatMessage flag");
    return 0;
  }
  if (width && width != 0xffffffffu) {
    // wraps at an explicit width; that core is not implemented.
    Fail("FormatMessage line width is not implemented");
    return 0;
  }
  if (!a[4]) {
    last_error = Win98FormatMessage::InvalidParameter;
    return 0;
  }
  char format[8192], output[8192];
  const auto source = Win98FormatMessage::Select(a[0]);
  if (source == Win98FormatMessage::None) {
    last_error = Win98FormatMessage::InvalidParameter;
    return 0;
  }
  if (source == Win98FormatMessage::String) {
    const char *text = ReadString(a[1]);
    if (strlen(text) >= sizeof format) { last_error = 122; return 0; }
    strcpy(format, text);
  } else {
    const unsigned identifier = Win98FormatMessage::Unwrap(a[0], a[2]);
    // NEO98 resolves FROM_SYSTEM against the generated original resource.
    bool found = false;
    if (source == Win98FormatMessage::Module)
      found = ModuleMessage(a[1], identifier, a[3] & 65535, format, sizeof format);
    // a message the module does not carry drops the module bit
    // and searches the system table, but only when the caller set both bits.
    if (!found && source == Win98FormatMessage::Module && !Win98FormatMessage::Retry(a[0])) {
      last_error = Win98FormatMessage::MessageNotFound;
      return 0;
    }
    if (!found && !MessageText(Win98MessageTable::Data, Win98MessageTable::Bytes, identifier,
                               format, sizeof format)) {
      last_error = Win98FormatMessage::MessageNotFound;
      return 0;
    }
  }
  unsigned written = 0, arguments = 0, consumed = 0;
  auto append = [&](const char *text, unsigned size) {
    if (size > 8191 - written)
      return false;
    memcpy(output + written, text, size);
    written += size;
    return true;
  };
  for (unsigned i = 0; format[i];) {
    if (format[i] != '%') {
      // Literal CR, LF and CRLF each describe one source line break.
      // Width 255 replaces each break with a space; it must not collapse
      // adjacent blank lines or consume an explicit %n insert.
      bool okay;
      if (format[i] == '\r' || format[i] == '\n') {
        const char first = format[i++];
        if (first == '\r' && format[i] == '\n') ++i;
        okay = width == 0xffffffffu ? append(" ", 1) : append("\r\n", 2);
      } else okay = append(format + i++, 1);
      if (!okay) {
        last_error = 122;
        return 0;
      }
      continue;
    }
    unsigned begin = i++;
    char ch = format[i++];
    if (!ch) {
      last_error = 87;
      return 0;
    }
    if (ch == '0')
      break;
    if (ch == 'n' || ch == 'r' || ch == 't' || ch == '%') {
      const char *value = ch == 'n'   ? "\r\n"
                          : ch == 'r' ? "\r"
                          : ch == 't' ? "\t"
                                      : "%";
      if (!append(value, strlen(value))) {
        last_error = 122;
        return 0;
      }
      continue;
    }
    if (ch < '1' || ch > '9') {
      // A percent also quotes a literal character. MSIAV.OCX's version
      // template is "%1!d!%.%2!d!%.%3!d!%.%4!d!"; rejecting the quoted
      // periods makes MFC throw CMemoryException.
      if (!append(&ch, 1)) { last_error = 122; return 0; }
      continue;
    }
    unsigned index = ch - '0';
    if (format[i] >= '0' && format[i] <= '9')
      index = index * 10 + format[i++] - '0';
    char spec[24] = "s";
    if (format[i] == '!') {
      ++i;
      unsigned n = 0;
      while (format[i] && format[i] != '!' && n < sizeof spec - 1)
        spec[n++] = format[i++];
      spec[n] = 0;
      if (format[i++] != '!') {
        last_error = 87;
        return 0;
      }
    }
    if (a[0] & 0x200) {
      if (!append(format + begin, i - begin)) {
        last_error = 122;
        return 0;
      }
      continue;
    }
    if (!a[6]) {
      last_error = Win98FormatMessage::InvalidParameter;
      return 0;
    }
    // without FORMAT_MESSAGE_ARGUMENT_ARRAY the parameter points
    // at the va_list, not at the values.
    if (!arguments) {
      arguments = Win98FormatMessage::IndirectArguments(a[0]) ? Read32(a[6]) : a[6];
      if (!arguments) {
        last_error = Win98FormatMessage::InvalidParameter;
        return 0;
      }
    }
    if (index > consumed)
      consumed = index;
    unsigned argument = Read32(arguments + (index - 1) * 4);
    if (!strcmp(spec, "s") || !strcmp(spec, "hs")) {
      const char *text = ReadString(argument);
      if (!append(text, strlen(text))) {
        last_error = 122;
        return 0;
      }
    } else {
      // A leading '-' left-aligns in the field, as wvsprintf takes it
      // (NETSTAT -s: "%1!-10u!").
      const bool left = spec[0] == '-';
      const char *digits_at = spec + (left ? 1 : 0);
      unsigned p = 0;
      while (digits_at[p] >= '0' && digits_at[p] <= '9')
        ++p;
      if (p > 3) {
        last_error = 87;
        return 0;
      }
      unsigned width = 0;
      for (unsigned j = 0; j < p; ++j)
        width = width * 10 + digits_at[j] - '0';
      if (width > 128) {
        last_error = 87;
        return 0;
      }
      unsigned digits = p;
      if (digits_at[p] == 'l' || digits_at[p] == 'h')
        ++p;
      char kind = digits_at[p];
      if (!kind || digits_at[p + 1] ||
          (kind != 'd' && kind != 'i' && kind != 'u' && kind != 'x' &&
           kind != 'X' && kind != 'c' && kind != 's' && kind != 'S')) {
        last_error = 87;
        return 0;
      }
      if (kind == 's' || kind == 'S') {
        // "%1!15s!": a string right-aligned in the field, as ROUTE.EXE's
        // route rows (messages 271Bh and 2726h) print their addresses. The
        // insert goes through wvsprintf in the original, so the text is
        // never cut to the width.
        char converted[8192];
        const bool wide = kind == 'S' ? digits_at[digits] != 'h' : digits_at[digits] == 'l';
        const char *text;
        if (wide) {
          unsigned n = 0;
          for (;;) {
            unsigned short ch = 0;
            if (!Read(argument + n * 2, &ch, sizeof ch)) return 0;
            if (!ch) break;
            if (n == sizeof converted - 1) { last_error = 122; return 0; }
            converted[n++] = char(AnsiNarrow(ch));
          }
          converted[n] = 0;
          text = converted;
        } else text = ReadString(argument);
        const unsigned length = (unsigned)strlen(text);
        char padded[132];
        unsigned n = 0;
        for (; n + length < width && n < sizeof padded; ++n) padded[n] = ' ';
        if (left ? !append(text, length) || !append(padded, n) : !append(padded, n) || !append(text, length)) {
          last_error = 122;
          return 0;
        }
        continue;
      }
      if (kind == 'c') {
        // "%1!c!": the argument is one character (SHELL32's drive name
        // template "%2 (%1!c!:)" passes the drive letter this way).
        char padded[132];
        unsigned n = 0;
        for (; n + 1 < width; ++n) padded[n] = ' ';
        padded[n++] = (char)argument;
        if (!append(padded, n)) {
          last_error = 122;
          return 0;
        }
        continue;
      }
      char printf_spec[16] = {'%'};
      unsigned at = 1;
      if (left) printf_spec[at++] = '-';
      memcpy(printf_spec + at, digits_at, digits);
      printf_spec[at + digits] = kind;
      printf_spec[at + digits + 1] = 0;
      char number[160];
      int size =
          (kind == 'd' || kind == 'i')
              ? snprintf(number, sizeof number, printf_spec, (int)argument)
              : snprintf(number, sizeof number, printf_spec, argument);
      if (size < 0 || (unsigned)size >= sizeof number ||
          !append(number, size)) {
        last_error = 122;
        return 0;
      }
    }
  }
  output[written] = 0;
  // the original leaves the caller's va_list advanced past every
  // argument it consumed. It fills its cache one slot at a time up to the
  // highest insert number it reaches.
  if (consumed && Win98FormatMessage::IndirectArguments(a[0]))
    Write32(a[6], arguments + consumed * 4);
  unsigned target = a[4];
  if (a[0] & 0x100) {
    target = Allocate(a[5] > written + 1 ? a[5] : written + 1);
    if (!target)
      return 0;
    Write32(a[4], target);
  } else if (a[5] <= written) {
    last_error = Win98FormatMessage::InsufficientBuffer;
    return 0;
  }
  Store(target, output, written + 1);
  return written;
}
} // namespace X86
