/*
 *
 * Copyright 2021-2024 Software Radio Systems Limited
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the distribution.
 *
 */

#pragma once

#include "srsran/adt/optional.h"
#include "fmt/format.h"
#include <optional>

namespace srsran {

/// Converts fmt memory buffer to c_str() without the need for conversion to intermediate std::string.
template <size_t N>
const char* to_c_str(fmt::basic_memory_buffer<char, N>& mem_buffer)
{
  mem_buffer.push_back('\0');
  return mem_buffer.data();
}

/// \brief Format helper used to insert delimiters between structure fields.
///
/// This class is used to format the structures used by the channel processors. Its methods can be called from an \c fmt
/// custom formatter specialization to aid in format argument parsing and formatting of the structure fields. It
/// automatically inserts the chosen delimiter between the formatted fields.
/// \note The default delimiter is a space character, however, a new line delimiter can be selected by formatting with
/// the \c n format specifier, as in <tt>{:n}</tt>.
/// \note A short formatting mode can be selected by formatting with the \c s format specifier, as in <tt>{:s}</tt>.
/// This option can be used to exclude some of the structure fields for a more compact representation.
class delimited_formatter
{
public:
  /// Default constructor.
  delimited_formatter()
  {
    static constexpr std::string_view DEFAULT_FORMAT    = "{}";
    static constexpr std::string_view DEFAULT_DELIMITER = " ";
    format_buffer.append(DEFAULT_FORMAT.begin(), DEFAULT_FORMAT.end());
    delimiter_buffer.append(DEFAULT_DELIMITER.begin(), DEFAULT_DELIMITER.end());
  }

  /// Constructor that sets the default delimiter according to the string \c default_delimiter.
  explicit delimited_formatter(std::string_view default_delimiter)
  {
    static constexpr std::string_view DEFAULT_FORMAT    = "{}";
    std::string_view                  DEFAULT_DELIMITER = default_delimiter;
    format_buffer.append(DEFAULT_FORMAT.begin(), DEFAULT_FORMAT.end());
    delimiter_buffer.append(DEFAULT_DELIMITER.begin(), DEFAULT_DELIMITER.end());
  }

  /// \brief Parsing helper for format specifiers.
  ///
  /// This helper detects the \c s and \c n short mode and new line delimiter specifiers. It also generates a format
  /// string with the parsed specifiers that can be used to propagate the formatting options to nested structures or
  /// structure fields.
  ///
  /// \tparam ParseContext Parse context type.
  /// \param[in] context A character range including the format specifiers.
  /// \return An iterator at the end of the parsed range.
  template <typename ParseContext>
  auto parse(ParseContext& context)
  {
    // Set the first field indicator.
    first = true;

    static constexpr std::string_view PREAMBLE_FORMAT   = "{:";
    static constexpr std::string_view NEWLINE_DELIMITER = "\n  ";

    // Skip if context is empty and use default format.
    if (context.begin() == context.end()) {
      return context.end();
    }

    format_buffer.clear();
    format_buffer.append(PREAMBLE_FORMAT.begin(), PREAMBLE_FORMAT.end());

    for (auto& it : context) {
      switch (it) {
        case 'n':
          // New line delimiter.
          delimiter_buffer.clear();
          delimiter_buffer.append(NEWLINE_DELIMITER.begin(), NEWLINE_DELIMITER.end());
          break;
        case 's':
          // Short representation.
          verbose = false;
          break;
        case '}':
          format_buffer.push_back(it);
          return &it;
      }
      // Propagate formatting to underlying structures.
      format_buffer.push_back(it);
    }
    // No end of context was found.
    return context.end();
  }

  /// \brief Formats one or more fields with the provided formatting options.
  ///
  /// Generates a string representation of the structure fields, according to the provided \c format formatting string,
  /// and writes it into the output iterator of the formatting \c context.
  ///
  /// \note The fields formatted with this method will be present in the default and the short form structure
  /// representations.
  ///
  /// \tparam FormatContext Format context type.
  /// \tparam Args Formatting arguments parameter pack.
  /// \param[in] context Formatting context, including an output iterator used to write the formatted representation.
  /// \param[in] format Formatting string, containing format specifiers.
  /// \param[in] args Fields to be formatted.
  template <typename FormatContext, typename... Args>
  void format_always(FormatContext& context, const char* format, Args&&... args)
  {
    this->format_to(context, format, std::forward<Args>(args)...);
  }

  /// \brief Formats a single field or nested structure with the parsed formatting options.
  ///
  /// Generates a string representation of the structure field, according to the previously parsed formatting string,
  /// and writes it into the output iterator of the formatting \c context. It can be used to propagate the parsed
  /// formatting options to nested structures.
  ///
  /// \note The fields formatted with this method will be present in the default and the short form structure
  /// representations.
  ///
  /// \tparam FormatContext Format context type.
  /// \tparam Args Formatting arguments parameter pack.
  /// \param[in] context Formatting context, including an output iterator used to write the formatted representation.
  /// \param[in] args Fields to be formatted.
  template <typename FormatContext, typename... Args>
  void format_always(FormatContext& context, Args&&... args)
  {
    this->format_to(context, std::forward<Args>(args)...);
  }

  /// \brief Formats one or more fields with the provided formatting options.
  ///
  /// Generates a string representation of the structure fields, according to the provided \c format formatting string,
  /// and writes it into the output iterator of the formatting \c context.
  ///
  /// \note The fields formatted with this method will not be present in the short form structure representation.
  ///
  /// \tparam FormatContext Format context type.
  /// \tparam Args Formatting arguments parameter pack.
  /// \param[in] context Formatting context, including an output iterator used to write the formatted representation.
  /// \param[in] format Formatting string, containing format specifiers.
  /// \param[in] args Fields to be formatted.
  template <typename FormatContext, typename... Args>
  void format_if_verbose(FormatContext& context, const char* format, Args&&... args)
  {
    if (verbose) {
      this->format_to(context, format, std::forward<Args>(args)...);
    }
  }

  /// \brief Formats a single field or nested structure with the parsed formatting options.
  ///
  /// Generates a string representation of the structure field, according to the previously parsed formatting string,
  /// and writes it into the output iterator of the formatting \c context. It can be used to propagate the parsed
  /// formatting options to nested structures.
  ///
  /// \note The fields formatted with this method will not be present in the short form structure representation.
  ///
  /// \tparam FormatContext Format context type.
  /// \tparam Args Formatting arguments parameter pack.
  /// \param[in] context Formatting context, including an output iterator used to write the formatted representation.
  /// \param[in] args Fields to be formatted.
  template <typename FormatContext, typename... Args>
  void format_if_verbose(FormatContext& context, Args&&... args)
  {
    if (verbose) {
      this->format_to(context, std::forward<Args>(args)...);
    }
  }
  /// \brief Returns \c true if the verbose representation is selected, \c false otherwise.
  /// \remark it must be called after \ref parse.
  bool is_verbose() const { return verbose; }

private:
  /// Internal method used to format with any formatting options.
  template <typename FormatContext, typename... Args>
  void format_to(FormatContext& context, const char* format, Args&&... args)
  {
    if (!first) {
      // Buffer to hold the formatted string.
      fmt::memory_buffer temp_buffer;
      fmt::format_to(temp_buffer, format, std::forward<Args>(args)...);

      if (temp_buffer.size() > 0) {
        // Prepend delimiter to the formatted output.
        fmt::format_to(context.out(), "{}", std::string_view(delimiter_buffer.data(), delimiter_buffer.size()));
      }

      // Append the formatted string to the context iterator.
      fmt::format_to(context.out(), "{}", std::string_view(temp_buffer.data(), temp_buffer.size()));

      return;
    }
    // Format without prepending delimiter.
    fmt::format_to(context.out(), format, std::forward<Args>(args)...);
    first = false;
  }

  /// Internal method used to format with the parsed formatting options.
  template <typename FormatContext, typename... Args>
  void format_to(FormatContext& context, Args&&... args)
  {
    if (!first) {
      // Buffer to hold the formatted string.
      fmt::memory_buffer temp_buffer;
      fmt::format_to(
          temp_buffer, std::string_view(format_buffer.data(), format_buffer.size()), std::forward<Args>(args)...);

      if (temp_buffer.size() > 0) {
        // Prepend delimiter to the formatted output.
        fmt::format_to(context.out(), "{}", std::string_view(delimiter_buffer.data(), delimiter_buffer.size()));
      }

      // Append the formatted string to the context iterator.
      fmt::format_to(context.out(), "{}", std::string_view(temp_buffer.data(), temp_buffer.size()));

      return;
    }
    // Format without prepending delimiter.
    fmt::format_to(
        context.out(), std::string_view(format_buffer.data(), format_buffer.size()), std::forward<Args>(args)...);
    first = false;
  }

  /// First field flag. It is used to determine when to insert delimiters between fields.
  bool first = true;
  /// Verbose flag. Verbose format option includes all the fields of the structure in the formatted output.
  bool verbose = true;

  /// Delimiter string.
  fmt::memory_buffer delimiter_buffer;
  /// Format string, used to propagate formatting options to nested structures.
  fmt::memory_buffer format_buffer;
};

namespace detail {

template <typename FormatFunc>
class custom_formattable
{
public:
  custom_formattable(FormatFunc func_) : format(std::move(func_)) {}

  /// Functor that takes a "FormatContext& ctx" as argument.
  FormatFunc format;
};

} // namespace detail

/// \brief Creates a formattable object, whose format function is the passed functor/lambda \c func.
///
/// This function is useful to defer the formatting logic to the point where the format is actually needed (e.g.
/// logging backend).
/// \tparam FormatFunc Type of the format function.
/// \param func Format function to be called when formatting is needed.
/// \return Formattable object.
template <typename FormatFunc>
detail::custom_formattable<FormatFunc> make_formattable(FormatFunc func)
{
  return detail::custom_formattable<FormatFunc>(std::move(func));
}

namespace detail {

/// Formattable object that prepends a prefix (e.g. "snr=")  to a given value. If the value is not set, the prefix is
/// not logged.
template <typename T>
struct optional_prefix_formatter {
  optional_prefix_formatter(const char* label_, const std::optional<T>& value_) : prefix(label_), value(value_) {}
  optional_prefix_formatter(const char* label_, std::optional<T>&& value_) : prefix(label_), value(std::move(value_)) {}
  const char*      prefix;
  std::optional<T> value;
};

} // namespace detail

/// \brief Creates a formattable object that prefixes an optional value (e.g. " snr=<value>"), if the optional set.
///
/// This is useful to defer the formatting logic to the point where the format is actually needed (e.g. logging
/// backend).
template <typename T>
detail::optional_prefix_formatter<T> add_prefix_if_set(const char* prefix, const std::optional<T>& value)
{
  return detail::optional_prefix_formatter<T>(prefix, value);
}
template <typename T>
detail::optional_prefix_formatter<T> add_prefix_if_set(const char* prefix, std::optional<T>&& value)
{
  return detail::optional_prefix_formatter<T>(prefix, std::move(value));
}

} // namespace srsran

namespace fmt {

struct basic_fmt_parser {
  template <typename ParseContext>
  auto parse(ParseContext& ctx) -> decltype(ctx.begin())
  {
    return ctx.begin();
  }
};

template <typename FormatFunc>
struct formatter<srsran::detail::custom_formattable<FormatFunc>> : public basic_fmt_parser {
public:
  template <typename FormatContext>
  auto format(const srsran::detail::custom_formattable<FormatFunc>& f, FormatContext& ctx)
  {
    return f.format(ctx);
  }
};

template <typename T>
struct formatter<srsran::detail::optional_prefix_formatter<T>> : public formatter<std::optional<T>> {
  template <typename FormatContext>
  auto format(const srsran::detail::optional_prefix_formatter<T>& f, FormatContext& ctx)
  {
    if (f.value.has_value()) {
      return fmt::format_to(ctx.out(), "{}{}", f.prefix, f.value);
    }
    return ctx.out();
  }
};

} // namespace fmt
