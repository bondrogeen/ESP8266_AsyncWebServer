
#define deviceAddress 0x57         // Адресс микросхемы по умолчанию, А0-А2 подключены к GND (или висят в воздухе, что не желательно).
#define pageSize 8                // смотрите даташит на свою микросхему 24Схххх, размер страницы может быть 32 байта, 64 так и 128 байт.
#define waitForWrite 15             // время миллисекунд для физической зарядки ячеек памяти, зависит от типа микросхемы, смотрите даташит


/*
  Физический буффер i2c шины в Ардуино равен 32 байта. Поэтому пишем блоками по 16 байт, но целую страницу за один вызов функции записи,
  а читаем блоками по 32 байта, но целую страницу за один вызов функции чтения. Таким образом удается воспользоваться страничным режимом,
  что роезко увеличивает скорость чтения\записи в ЕЕПРОМ, поддерживающую страничный режим  записи\чтения данных.
*/

template <class T> void EEPROM_get(unsigned long eeaddress, T& value) {
  uint8_t num_bytes = sizeof(value);
  byte* p = (byte*)(void*)&value;
  byte countChank = num_bytes / 32;
  byte restChank = num_bytes % 32;
  unsigned long addressChank = 0;
  if (countChank > 0) {
    for (byte i = 0; i < countChank; i++) {
      addressChank = eeaddress + 32 * i;
      Wire.beginTransmission(deviceAddress);
      Wire.write((unsigned long)(addressChank >> 8));
      Wire.write((unsigned long)(addressChank & 0xFF));
      Wire.endTransmission();
      Wire.requestFrom(deviceAddress, 32);
      while (Wire.available()) *p++ = Wire.read();
    }
  }
  if (restChank > 0) {
    if (countChank > 0) addressChank += 32;

    Wire.beginTransmission(deviceAddress);
    Wire.write((unsigned long)((addressChank) >> 8));
    Wire.write((unsigned long)((addressChank) & 0xFF));
    Wire.endTransmission();
    Wire.requestFrom(deviceAddress, restChank);
    while (Wire.available()) *p++ = Wire.read();
  }

}



template <class T> void  EEPROM_put(unsigned long eeaddress, const T& value) {
  const byte* p = (const byte*)(const void*)&value;

  byte counter = 0;
  unsigned long address;
  byte page_space;
  byte page = 0;
  byte num_writes;
  uint16_t data_len = 0;
  byte first_write_size;
  byte last_write_size;
  byte write_size;

  // Calculate length of data
  data_len = sizeof(value);

  // Calculate space available in first page
  page_space = int(((eeaddress / pageSize) + 1) * pageSize) - eeaddress;

  // Calculate first write size
  if (page_space > 16) {
    first_write_size = page_space - ((page_space / 16) * 16);
    if (first_write_size == 0) {
      first_write_size = 16;
    }
  }
  else {
    first_write_size = page_space;
  }

  // calculate size of last write
  if (data_len > first_write_size) {
    last_write_size = (data_len - first_write_size) % 16;
  }

  // Calculate how many writes we need
  if (data_len > first_write_size) {
    num_writes = ((data_len - first_write_size) / 16) + 2;
  }
  else {
    num_writes = 1;
  }

 
  address = eeaddress;
  for (page = 0; page < num_writes; page++)   {
    if (page == 0) {
      write_size = first_write_size;
    }
    else if (page == (num_writes - 1)) {
      write_size = last_write_size;
    }
    else {
      write_size = 16;
    }

    Wire.beginTransmission(deviceAddress);
    Wire.write((unsigned long)((address) >> 8));
    Wire.write((unsigned long)((address) & 0xFF));
    counter = 0;
    do {
      Wire.write((byte) *p++);
      counter++;
    } while ((counter < write_size));
    Wire.endTransmission();
    address += write_size;                      // увеличиваем адрес для следующего буфера
    delay(waitForWrite);                        // задержка нужна для того, чтобы ЕЕПРОМ успела зарядить ячейки памяти
  }
}