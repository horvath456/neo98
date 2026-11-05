// SPDX-License-Identifier: GPL-2.0-or-later
#include "win98_arrays.h"
namespace Win98Arrays {
bool Memory::Move(U destination, U source, U bytes) {
  if (bytes > 0x2000000u || destination > ~0u - bytes || source > ~0u - bytes)
    return false;
  unsigned char block[256];
  if (destination > source && destination < source + bytes) {
    while (bytes) {
      U n = bytes > sizeof block ? sizeof block : bytes;
      bytes -= n;
      if (!Read(source + bytes, block, n) ||
          !Write(destination + bytes, block, n))
        return false;
    }
  } else
    for (U at = 0; at < bytes;) {
      U n = bytes - at;
      if (n > sizeof block)
        n = sizeof block;
      if (!Read(source + at, block, n) || !Write(destination + at, block, n))
        return false;
      at += n;
    }
  return true;
}
namespace {
// Five original DWORDs: count, data, capacity/item-size fields, growth.
struct Array {
  U count, data, third, fourth, growth;
};
bool Load(Memory &m, U handle, Array &a) {
  return handle && m.Read(handle, &a, sizeof a);
}
void Save(Memory &m, U handle, const Array &a) {
  m.Write(handle, &a, sizeof a);
}
bool Size(U count, U size, U &bytes) {
  if (!size || count > 0x2000000u / size)
    return false;
  bytes = count * size;
  return true;
}
U Create(Memory &m, bool pointers, U size, U growth, U heap) {
  U h = m.Resize(pointers ? heap : 1, 0, 20);
  if (!h)
    return 0;
  Array a = {};
  if (pointers) {
    a.third = heap;
    a.growth = (int)growth < 8 ? 8 : growth;
  } else {
    a.fourth = size;
    a.growth = growth ? growth : 1;
  }
  Save(m, h, a);
  return h;
}
bool Grow(Memory &m, Array &a, U wanted) {
  if ((int)a.fourth >= (int)wanted)
    return true;
  if (!a.growth || wanted > 0x7fffffffu - a.growth + 1)
    return false;
  U capacity = ((wanted - 1 + a.growth) / a.growth) * a.growth, bytes;
  if (!Size(capacity, 4, bytes))
    return false;
  U data = m.Resize(a.third, a.data, bytes);
  if (!data)
    return false;
  a.data = data;
  a.fourth = capacity;
  if (a.growth < 256)
    a.growth *= 2;
  return true;
}
void Sort(Memory &m, U data, U first, U count, U temporary, U compare,
          U parameter) {
  if (count < 2)
    return;
  U start = data + first * 4, left = m.Word(start), right = m.Word(start + 4);
  if (count == 2) {
    if (m.Compare(compare, left, right, parameter) > 0) {
      m.Word(temporary, left);
      m.Word(start, right);
      m.Word(start + 4, left);
    }
    return;
  }
  U half = count / 2;
  Sort(m, data, first, half, temporary, compare, parameter);
  Sort(m, data, first + half, count - half, temporary, compare, parameter);
  m.Move(temporary, start, half * 4);
  U l = 0, r = half, out = 0;
  while (l < half) {
    left = m.Word(temporary + l * 4);
    right = m.Word(start + r * 4);
    if (m.Compare(compare, left, right, parameter) > 0) {
      m.Word(start + out * 4, right);
      ++out;
      ++r;
      if (r == count) {
        m.Move(start + out * 4, temporary + l * 4, (count - out) * 4);
        return;
      }
    } else {
      m.Word(start + out * 4, left);
      ++out;
      ++l;
    }
  }
}
U Search(Memory &m, const Array &a, const U *v) {
  U value = v[1], compare = v[3], parameter = v[4], flags = v[5];
  if (!(flags & 1)) {
    for (U i = v[2]; i < a.count; ++i)
      if (!m.Compare(compare, value, m.Word(a.data + i * 4), parameter))
        return i;
    return ~0u;
  }
  int low = 0, high = (int)a.count - 1, mid = 0, result = 0;
  while (low <= high) {
    mid = (low + high) / 2;
    result = m.Compare(compare, value, m.Word(a.data + mid * 4), parameter);
    if (result < 0)
      high = mid - 1;
    else if (result > 0)
      low = mid + 1;
    else {
      while (mid > 0 && !m.Compare(compare, value,
                                   m.Word(a.data + (mid - 1) * 4), parameter))
        --mid;
      return mid;
    }
  }
  return flags & 6 ? (U)(result > 0 ? low : mid) : ~0u;
}
} // namespace
U Call(Memory &m, U ordinal, const U *v) {
  if (ordinal == 320)
    return Create(m, false, v[0], v[1], 1);
  if (ordinal == 328 || ordinal == 340)
    return Create(m, true, 0, v[0], ordinal == 340 && v[1] ? v[1] : 1);
  const bool pointers = ordinal >= 328;
  Array a = {};
  U h = v[0];
  if ((ordinal == 321 || ordinal == 329) && !h)
    return 1;
  if (!Load(m, h, a))
    return ordinal == 324 || ordinal == 334 || ordinal == 333 || ordinal == 339
               ? 0xffffffff
               : 0;
  U capacity = pointers ? a.fourth : a.third, unit = pointers ? 4 : a.fourth,
    heap = pointers ? a.third : 1;
  U index = ordinal == 321 || ordinal == 329 || ordinal == 327 || ordinal == 337
                ? 0
                : v[1];
  switch (ordinal) {
  case 321:
  case 329:
    if (a.data && !m.Free(heap, a.data))
      return 0;
    return m.Free(heap, h);
  case 322:
  case 323:
  case 332:
    if ((int)index < 0 || index >= a.count)
      return 0;
    if (ordinal == 322)
      return m.Move(v[2], a.data + index * unit, unit);
    return ordinal == 323 ? a.data + index * unit : m.Word(a.data + index * 4);
  case 324:
  case 334: {
    if ((int)index < 0)
      return 0xffffffff;
    if (index > a.count)
      index = a.count;
    if (a.count >= 0x1000000)
      return 0xffffffff;
    if (capacity < a.count + 1) {
      if (pointers) {
        if (!Grow(m, a, a.count + 1))
          return 0xffffffff;
      } else {
        U bytes;
        if (capacity > ~0u - a.growth ||
            !Size(capacity + a.growth, unit, bytes))
          return 0xffffffff;
        U data = m.Resize(heap, a.data, bytes);
        if (!data)
          return 0xffffffff;
        a.data = data;
        a.third = capacity + a.growth;
      }
    }
    if (index < a.count &&
        !m.Move(a.data + (index + 1) * unit, a.data + index * unit,
                (a.count - index) * unit))
      return 0xffffffff;
    if (pointers)
      m.Word(a.data + index * 4, v[2]);
    else
      m.Move(a.data + index * unit, v[2], unit);
    ++a.count;
    Save(m, h, a);
    return index;
  }
  case 325:
  case 335: {
    if ((int)index < 0 || index >= 0x1000000)
      return 0;
    if (a.count <= index) {
      if (pointers) {
        if (!Grow(m, a, index + 1))
          return 0;
      } else if (capacity < index + 1) {
        U bytes;
        if (!a.growth || index > ~0u - a.growth)
          return 0;
        U wanted = ((index + a.growth) / a.growth) * a.growth;
        if (!Size(wanted, unit, bytes))
          return 0;
        U data = m.Resize(heap, a.data, bytes);
        if (!data)
          return 0;
        a.data = data;
        a.third = wanted;
      }
      a.count = index + 1;
    }
    if (pointers)
      m.Word(a.data + index * 4, v[2]);
    else
      m.Move(a.data + index * unit, v[2], unit);
    Save(m, h, a);
    return 1;
  }
  case 326:
  case 336: {
    if ((int)index < 0 || index >= a.count)
      return 0;
    U result = pointers ? m.Word(a.data + index * 4) : 1;
    if (index < a.count - 1)
      m.Move(a.data + index * unit, a.data + (index + 1) * unit,
             (a.count - index - 1) * unit);
    --a.count;
    if (a.growth < capacity - a.count) {
      U wanted = capacity - a.growth, bytes;
      if (!Size(wanted, unit, bytes))
        return 0;
      // The original also replaces its pointer when shrink realloc fails.
      a.data = m.Resize(heap, a.data, bytes);
      if (pointers)
        a.fourth = wanted;
      else
        a.third = wanted;
    }
    Save(m, h, a);
    return result;
  }
  case 327:
  case 337:
    if (a.data && !m.Free(heap, a.data))
      return 0;
    a.count = a.data = 0;
    if (pointers)
      a.fourth = 0;
    else
      a.third = 0;
    Save(m, h, a);
    return 1;
  case 330: {
    bool result = Grow(m, a, index);
    Save(m, h, a);
    return result;
  }
  case 331: {
    U destination = index;
    const bool created = !destination;
    if (created)
      destination = Create(m, true, 0, a.growth, a.third);
    if (!destination)
      return 0;
    Array target = {};
    if (!Load(m, destination, target))
      return 0;
    if (!Grow(m, target, a.fourth)) {
      // Preserve the original clone failure branch, including its choice of
      // target.
      if (!created) {
        U destroy[] = {destination, 0};
        Call(m, 329, destroy);
      }
      return 0;
    }
    target.count = a.count;
    m.Move(target.data, a.data, a.count * 4);
    Save(m, destination, target);
    return destination;
  }
  case 333:
    if (!a.data)
      return 0xffffffff;
    for (U i = 0; i < a.count; ++i)
      if (m.Word(a.data + i * 4) == index)
        return i;
    return 0xffffffff;
  case 338: {
    if (!a.count)
      return 1;
    if (a.count > 0x1000000)
      return 0;
    U scratch = m.Resize(1, 0, (a.count / 2) * 4);
    if (!scratch)
      return 0;
    Sort(m, a.data, 0, a.count, scratch, v[1], v[2]);
    m.Free(1, scratch);
    return 1;
  }
  case 339:
    return Search(m, a, v);
  default:
    return 0;
  }
}
} // namespace Win98Arrays
