#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>

namespace minisgbd {

constexpr std::size_t PERSON_NAME_STORAGE_SIZE = 64;
constexpr std::size_t PERSON_CITY_STORAGE_SIZE = 48;
constexpr std::size_t PERSON_PROFESSION_STORAGE_SIZE = 64;

constexpr std::size_t PERSON_NAME_MAX_LENGTH =
    PERSON_NAME_STORAGE_SIZE - 1;
constexpr std::size_t PERSON_CITY_MAX_LENGTH =
    PERSON_CITY_STORAGE_SIZE - 1;
constexpr std::size_t PERSON_PROFESSION_MAX_LENGTH =
    PERSON_PROFESSION_STORAGE_SIZE - 1;

struct PersonRecord {
  int32_t id{0};
  std::string nombre;
  std::string ciudad;
  std::string profesion;
};

inline void ValidatePersonText(const std::string &value,
                               std::size_t maximum_length,
                               const char *column) {
  if (value.size() > maximum_length) {
    throw std::invalid_argument(
        std::string("El campo ") + column + " supera " +
        std::to_string(maximum_length) + " bytes.");
  }
  if (value.find('\0') != std::string::npos) {
    throw std::invalid_argument(
        std::string("El campo ") + column +
        " no puede contener bytes nulos.");
  }
}

inline void ValidatePersonRecord(const PersonRecord &person) {
  ValidatePersonText(
      person.nombre, PERSON_NAME_MAX_LENGTH, "nombre");
  ValidatePersonText(
      person.ciudad, PERSON_CITY_MAX_LENGTH, "ciudad");
  ValidatePersonText(
      person.profesion, PERSON_PROFESSION_MAX_LENGTH, "profesion");
}

inline bool operator==(const PersonRecord &left,
                       const PersonRecord &right) {
  return left.id == right.id && left.nombre == right.nombre &&
         left.ciudad == right.ciudad &&
         left.profesion == right.profesion;
}

inline bool operator!=(const PersonRecord &left,
                       const PersonRecord &right) {
  return !(left == right);
}

}  // namespace minisgbd
