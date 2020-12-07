template <class T> uint32_t writeAnything(uint8_t* arr, const T& value) {
    const byte * p = (const byte*) &value;
    uint32_t i;
    for (i = 0; i < sizeof value; i++) arr[i] = *p++;
    return i;
  }

template <class T> uint32_t readAnything(uint8_t* arr, T& value) {
    byte * p = (byte*) &value;
    uint32_t i;
    for (i = 0; i < sizeof value; i++) *p++ = arr[i];
    return i;
  }
