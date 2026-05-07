#pragma once
#ifndef CPP_TREE_SITTER_HPP
#define CPP_TREE_SITTER_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <ios>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

/////////////////////////////////////////////////////////////////////////////
// Standard Compatibility Macros
/////////////////////////////////////////////////////////////////////////////

#if defined(_MSVC_LANG)
#define TS_CXX_LEVEL _MSVC_LANG
#else
#define TS_CXX_LEVEL __cplusplus
#endif

#define TS_HAS_CXX17 (TS_CXX_LEVEL >= 201703L)
#define TS_HAS_CXX20 (TS_CXX_LEVEL >= 202002L)
#define TS_HAS_CXX23 (TS_CXX_LEVEL >= 202302L)

#if TS_HAS_CXX17
#include <filesystem>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>
#endif

#if TS_HAS_CXX20
#include <concepts>
#include <ranges>
#include <span>
#endif

#if TS_HAS_CXX23
#include <expected>
#endif

#if defined(_WIN32) && !defined(__CYGWIN__)
// On Windows we get descriptor using _fileno
#include <io.h>
#define TS_FILENO _fileno
// On Windows we create duplicate of descriptor using _dup
#define TS_DUP _dup
#else
// On POSIX systems we use fileno from <cstdio>
#include <cstdio>
#define TS_FILENO fileno
// On POSIX systems we use dup from <unistd.h>
#include <unistd.h>
#define TS_DUP dup
#endif

#include <tree_sitter/api.h>

namespace ts
{
    /////////////////////////////////////////////////////////////////////////////
    // Forward Declarations
    /////////////////////////////////////////////////////////////////////////////

    struct Point;
    struct InputEdit;
    struct Node;
    class TreeCursor;
    class LookaheadIterator;
    class QueryCursor;
#if defined(CPP_TREE_SITTER_FEATURE_WASM)
    class WasmStore;
#endif

#if !TS_HAS_CXX17
    class StringView
    {
    public:
        ////////////////////////////////////////////////////////////////
        // Lifecycle
        ////////////////////////////////////////////////////////////////

        constexpr StringView() noexcept : data_(nullptr), size_(0)
        {}

        constexpr StringView(const char *s, size_t count) noexcept : data_(s), size_(count)
        {}

        StringView(const char *s) : data_(s), size_(s ? std::strlen(s) : 0)
        {}

        StringView(const std::string &s) noexcept : data_(s.data()), size_(s.size())
        {}

        ////////////////////////////////////////////////////////////////
        // Data
        ////////////////////////////////////////////////////////////////

        [[nodiscard]] constexpr const char *data() const noexcept
        {
            return data_;
        }

        [[nodiscard]] constexpr const char *begin() const noexcept
        {
            return data_;
        }

        [[nodiscard]] constexpr const char *end() const noexcept
        {
            return data_ + size_;
        }

        ////////////////////////////////////////////////////////////////
        // Size
        ////////////////////////////////////////////////////////////////

        [[nodiscard]] constexpr size_t size() const noexcept
        {
            return size_;
        }

        [[nodiscard]] constexpr bool empty() const noexcept
        {
            return size_ == 0;
        }

        ////////////////////////////////////////////////////////////////
        // Manipulation
        ////////////////////////////////////////////////////////////////

        [[nodiscard]] StringView substr(size_t pos, size_t n) const
        {
            return { data_ + pos, n };
        }

        ////////////////////////////////////////////////////////////////
        // Comparision
        ////////////////////////////////////////////////////////////////

        [[nodiscard]] int compare(StringView v) const noexcept
        {
            const size_t rlen = (size_ < v.size_) ? size_ : v.size_;

            int result = std::char_traits<char>::compare(data_, v.data_, rlen);

            if (result == 0)
            {
                if (size_ < v.size_)
                {
                    return -1;
                }
                if (size_ > v.size_)
                {
                    return 1;
                }
            }
            return result;
        }

        [[nodiscard]] int compare(const char *s) const
        {
            return compare(StringView(s));
        }

        [[nodiscard]] int compare(std::string &s) const
        {
            return compare(StringView(s));
        }

        [[nodiscard]] friend bool operator==(StringView lhs, StringView rhs) noexcept
        {
            return lhs.size() == rhs.size() && lhs.compare(rhs) == 0;
        }

        [[nodiscard]] friend bool operator!=(StringView lhs, StringView rhs) noexcept
        {
            return !(lhs == rhs);
        }

        ////////////////////////////////////////////////////////////////
        // Operators
        ////////////////////////////////////////////////////////////////

        [[nodiscard]] constexpr char operator[](size_t pos) const
        {
            return data_[pos];
        }

        [[nodiscard]] explicit operator std::string() const
        {
            if (!data_ || size_ == 0)
            {
                return "";
            }
            return std::string(data_, size_);
        }

    private:
        const char *data_;
        size_t      size_;
    };
#endif

    /////////////////////////////////////////////////////////////////////////////
    // Aliases.
    // Create slightly stricter aliases for some of the core tree-sitter types.
    /////////////////////////////////////////////////////////////////////////////

    // Direct alias of { major_version: uint8_t; minor_version: uint8_t; patch_version: uint8_t }
    using LanguageMetadata = TSLanguageMetadata;

#if defined(CPP_TREE_SITTER_FEATURE_WASM)
    using WasmEngine = wasm_engine_t;
#endif
    using StateID = uint16_t;
    using Symbol  = uint16_t;
    using FieldID = uint16_t;
    using Version = uint32_t;
    using NodeID  = uintptr_t;

    /////////////////////////////////////////////////////////////////////////////
    // Helper Classes & Structs
    // Internal utilities for memory management and range representation.
    /////////////////////////////////////////////////////////////////////////////

    // An inclusive range representation
    template <typename T>
    struct Extent
    {
        T start;
        T end;

        Extent() noexcept : start(T()), end(T())
        {}

        Extent(T start_value, T end_value) : start(start_value), end(end_value)
        {}
    };

    namespace details
    {
#if TS_HAS_CXX17
        using StringViewParameter = const std::string_view;
        using StringViewReturn    = std::string_view;
        using ByteViewU8_t        = const std::basic_string_view<uint8_t>;

        template <typename T>
        using OptionalParam = std::optional<std::reference_wrapper<T>>;

        template <typename T, typename Raw>
        [[nodiscard]] static Raw *get_raw(OptionalParam<T> opt)
        {
            return opt.has_value() ? static_cast<Raw *>(opt->get()) : nullptr;
        }
#else
        using StringViewParameter = const StringView;
        using StringViewReturn    = StringView;

        template <typename T>
        using OptionalParam = T *;

        template <typename T, typename Raw>
        [[nodiscard]] static Raw *get_raw(OptionalParam<T> opt)
        {
            return opt ? static_cast<Raw *>(*opt) : nullptr;
        }
#endif

        struct QueryErrorHelper
        {
            static std::string format_error(uint32_t error_offset, const TSQueryError &error)
            {
                std::string type_name;
                switch (error)
                {
                    case TSQueryErrorSyntax:
                        type_name = "Syntax";
                        break;
                    case TSQueryErrorNodeType:
                        type_name = "Node Type";
                        break;
                    case TSQueryErrorField:
                        type_name = "Field";
                        break;
                    case TSQueryErrorCapture:
                        type_name = "Capture";
                        break;
                    case TSQueryErrorStructure:
                        type_name = "Structure";
                        break;
                    case TSQueryErrorLanguage:
                        type_name = "Language";
                        break;
                    default:
                        type_name = "Unknown";
                        break;
                }
                return "Tree-sitter Query Error at offset " + std::to_string(error_offset) + " (Type: " + type_name
                     + ")";
            }

            static void validate(uint32_t error_offset, TSQueryError &error)
            {
                if (error == TSQueryErrorNone)
                {
                    return;
                }

                throw std::runtime_error(format_error(error_offset, error));
            }
        };

#if defined(CPP_TREE_SITTER_FEATURE_WASM)
        struct WasmErrorHelper
        {
            static std::string format_error(const TSWasmError &error)
            {
                StringViewReturn kind_name;
                switch (error.kind)
                {
                    case TSWasmErrorKindParse:
                        kind_name = "Parse";
                        break;
                    case TSWasmErrorKindCompile:
                        kind_name = "Compile";
                        break;
                    case TSWasmErrorKindInstantiate:
                        kind_name = "Instantiate";
                        break;
                    case TSWasmErrorKindAllocate:
                        kind_name = "Allocate";
                        break;
                    default:
                        kind_name = "Unknown";
                        break;
                }

                std::string message = error.message ? error.message : "No message";

                return "Tree-sitter Wasm Error [" + std::string(kind_name) + "]: " + message;
            }

            static void validate(TSWasmError &error)
            {
                if (error.kind == TSWasmErrorKindNone)
                {
                    return;
                }

                std::string err_msg = format_error(error);

                if (error.message)
                {
                    std::free(error.message);
                }


                throw std::runtime_error(err_msg);
            }
        };

        [[nodiscard]] static std::string get_grammar_name_from_file(StringViewParameter path)
        {
#if TS_HAS_CXX17
            std::string name = std::filesystem::path(path).stem().string();
#else
            std::string filename = std::string(path);
            size_t      slash    = filename.find_last_of("/\\");
            size_t      dot      = filename.find_last_of('.');

            size_t start = (slash == std::string::npos) ? 0 : slash + 1;
            size_t end   = (dot == std::string::npos || dot < start) ? filename.length() : dot;

            std::string name = filename.substr(start, end - start);
#endif
            const std::string prefix = "tree-sitter-";

            if (name.compare(0, prefix.length(), prefix) == 0)
            {
                return name.substr(prefix.length());
            }

            return name;
        }
#endif

        [[nodiscard]] inline StringViewReturn make_view(const char *str)
        {
            return str ? StringViewReturn(str) : StringViewReturn();
        }

        [[nodiscard]] inline StringViewReturn make_view(const char *str, size_t length) noexcept
        {
            return (str && length > 0) ? StringViewReturn(str, length) : StringViewReturn();
        }

        template <typename TS_Options, typename TS_State, typename CPP_State>
        struct OptionsBase
        {
            using ProgressCallbackFunction = std::function<bool(CPP_State *)>;
            ProgressCallbackFunction progress_callback;

            OptionsBase()
            {
                impl          = std::make_unique<TS_Options>();
                impl->payload = this;
            }

            [[nodiscard]] static bool progress_callback_proxy(TS_State *state)
            {
                if (state && state->payload)
                {
                    auto *self = static_cast<OptionsBase *>(state->payload);
                    if (self->progress_callback)
                    {
                        CPP_State cpp_state(state);
                        return self->progress_callback(&cpp_state);
                    }
                }
                return false;
            }

            [[nodiscard]] operator TS_Options() const
            {
                TS_Options options{};
                options.payload           = const_cast<OptionsBase *>(this);
                options.progress_callback = (progress_callback)
                                                  ? reinterpret_cast<bool (*)(TS_State *)>(progress_callback_proxy)
                                                  : nullptr;
                return options;
            }

            [[nodiscard]] operator TS_Options *() const noexcept
            {
                impl->progress_callback = (progress_callback)
                                                ? reinterpret_cast<bool (*)(TS_State *)>(progress_callback_proxy)
                                                : nullptr;
                return impl.get();
            }

        private:
            std::unique_ptr<TS_Options> impl;
        };

        struct FreeHelper
        {
            template <typename T>
            inline void operator()(T *raw_pointer) const
            {
                if (raw_pointer == nullptr)
                {
                    return;
                }

                std::free(raw_pointer);
            }
        };

        template <>
        inline void FreeHelper::operator()<TSQueryCursor>(TSQueryCursor *raw_pointer) const
        {
            if (raw_pointer == nullptr)
            {
                return;
            }

            ts_query_cursor_delete(raw_pointer);
        }

        template <>
        inline void FreeHelper::operator()<TSLookaheadIterator>(TSLookaheadIterator *raw_pointer) const
        {
            if (raw_pointer == nullptr)
            {
                return;
            }

            ts_lookahead_iterator_delete(raw_pointer);
        }
    } // namespace details

#if TS_HAS_CXX17
    using DecodeFunction = std::function<uint32_t(details::ByteViewU8_t, int32_t *)>;
#else
    using DecodeFunction = std::function<uint32_t(const uint8_t *, uint32_t, int32_t *)>;
#endif

    /////////////////////////////////////////////////////////////////////////////
    // Enums.
    // Scoped enum wrappers for Tree-sitter constants to ensure type safety.
    /////////////////////////////////////////////////////////////////////////////

    enum class InputEncoding : uint8_t
    {
        UTF8    = TSInputEncodingUTF8,
        UTF16LE = TSInputEncodingUTF16LE,
        UTF16BE = TSInputEncodingUTF16BE,
        Custom  = TSInputEncodingCustom
    };

    enum class SymbolType : uint8_t
    {
        TypeRegular   = TSSymbolTypeRegular,
        TypeAnonymous = TSSymbolTypeAnonymous,
        TypeSupertype = TSSymbolTypeSupertype,
        TypeAuxiliary = TSSymbolTypeAuxiliary,
    };

    enum class LogType : uint8_t
    {
        Parse = TSLogTypeParse,
        Lex   = TSLogTypeLex
    };

    enum class Quantifier : uint8_t
    {
        Zero       = TSQuantifierZero,
        ZeroOrOne  = TSQuantifierZeroOrOne,
        ZeroOrMore = TSQuantifierZeroOrMore,
        One        = TSQuantifierOne,
        OneOrMore  = TSQuantifierOneOrMore,
    };

    enum class QueryPredicateStepType : uint8_t
    {
        Done    = TSQueryPredicateStepTypeDone,
        Capture = TSQueryPredicateStepTypeCapture,
        String  = TSQueryPredicateStepTypeString
    };

    /////////////////////////////////////////////////////////////////////////////
    // Point
    // A location in source code in terms of rows and columns.
    /////////////////////////////////////////////////////////////////////////////

    struct Point
    {
        uint32_t row;
        uint32_t column;

        ////////////////////////////////////////////////////////////////
        // Lifecycle
        ////////////////////////////////////////////////////////////////

        Point() noexcept : row(0), column(0)
        {}

        Point(uint32_t row_value, uint32_t column_value) noexcept : row(row_value), column(column_value)
        {}

        Point(const TSPoint &other) noexcept : row(other.row), column(other.column)
        {}

        ////////////////////////////////////////////////////////////////
        // Manipulation
        ////////////////////////////////////////////////////////////////

        // Definition deferred until after the definition of InputEdit.
        void edit(const InputEdit &edit, uint32_t &byte_offset);

        ////////////////////////////////////////////////////////////////
        // Comparision
        ////////////////////////////////////////////////////////////////

        [[nodiscard]] bool operator==(const Point &other) const noexcept
        {
            return row == other.row && column == other.column;
        }

#if !TS_HAS_CXX20
        [[nodiscard]] bool operator!=(const Point &other) const noexcept
        {
            return !(*this == other);
        }
#endif

        [[nodiscard]] bool operator>(const Point &other) const noexcept
        {
            return (row > other.row) || (row == other.row && column > other.column);
        }

        [[nodiscard]] bool operator<=(const Point &other) const noexcept
        {
            return !(*this > other);
        }

        [[nodiscard]] bool operator<(const Point &other) const noexcept
        {
            return (row < other.row) || (row == other.row && column < other.column);
        }

        [[nodiscard]] bool operator>=(const Point &other) const noexcept
        {
            return !(*this < other);
        }

        [[nodiscard]] static bool is_valid_range(const Point &start, const Point &end) noexcept
        {
            return start <= end;
        }

        ////////////////////////////////////////////////////////////////
        // Operators
        ////////////////////////////////////////////////////////////////

        [[nodiscard]] operator TSPoint() const noexcept
        {
            return { row, column };
        }
    };

    /////////////////////////////////////////////////////////////////////////////
    // InputEdit
    // Data describing an edit to a source code string.
    /////////////////////////////////////////////////////////////////////////////

    struct InputEdit
    {
        uint32_t start_byte;
        uint32_t old_end_byte;
        uint32_t new_end_byte;
        Point    start_point;
        Point    old_end_point;
        Point    new_end_point;

        ////////////////////////////////////////////////////////////////
        // Lifecycle
        ////////////////////////////////////////////////////////////////

        InputEdit() noexcept
            : start_byte(0)
            , old_end_byte(0)
            , new_end_byte(0)
            , start_point(Point{})
            , old_end_point(Point{})
            , new_end_point(Point{})
        {}

        InputEdit(uint32_t start_byte_value,
                  uint32_t old_end_byte_value,
                  uint32_t new_end_byte_value,
                  Point    start_point_value,
                  Point    old_end_point_value,
                  Point    new_end_point_value) noexcept
            : start_byte(start_byte_value)
            , old_end_byte(old_end_byte_value)
            , new_end_byte(new_end_byte_value)
            , start_point(start_point_value)
            , old_end_point(old_end_point_value)
            , new_end_point(new_end_point_value)
        {}

        explicit InputEdit(const TSInputEdit &edit) noexcept
            : start_byte(edit.start_byte)
            , old_end_byte(edit.old_end_byte)
            , new_end_byte(edit.new_end_byte)
            , start_point(edit.start_point)
            , old_end_point(edit.old_end_point)
            , new_end_point(edit.new_end_point)
        {}

        ////////////////////////////////////////////////////////////////
        // Validation
        ////////////////////////////////////////////////////////////////

        [[nodiscard]] bool isValid() const noexcept
        {
            return start_byte <= old_end_byte && Point::is_valid_range(start_point, old_end_point);
        }

        [[nodiscard]] void validate() const
        {
            if (!isValid())
            {
                throw std::invalid_argument("Tree-sitter: Invalid edit ranges (start > old_end)");
            }
        }

        ////////////////////////////////////////////////////////////////
        // Operators
        ////////////////////////////////////////////////////////////////

        [[nodiscard]] operator TSInputEdit() const noexcept
        {
            return { start_byte, old_end_byte, new_end_byte, start_point, old_end_point, new_end_point };
        }
    };

    inline void Point::edit(const InputEdit &edit, uint32_t &byte_offset)
    {
        edit.validate();

        TSPoint     raw_point = static_cast<TSPoint>(*this);
        TSInputEdit raw_edit  = static_cast<TSInputEdit>(edit);

        ts_point_edit(&raw_point, &byte_offset, &raw_edit);

        row    = raw_point.row;
        column = raw_point.column;
    }

    /////////////////////////////////////////////////////////////////////////////
    // Range
    // Representation of a range within the source code using points and bytes.
    /////////////////////////////////////////////////////////////////////////////

    struct Range
    {
        Extent<Point>    point;
        Extent<uint32_t> byte;

        ////////////////////////////////////////////////////////////////
        // Lifecycle
        ////////////////////////////////////////////////////////////////

        explicit Range(Extent<Point> point_value, Extent<uint32_t> byte_value) noexcept
            : point(point_value), byte(byte_value)
        {}

        explicit Range(const TSRange &range) noexcept
            : point(range.start_point, range.end_point), byte(range.start_byte, range.end_byte)
        {}

        ////////////////////////////////////////////////////////////////
        // Manipulation
        ////////////////////////////////////////////////////////////////

        void edit(const InputEdit &edit)
        {
            edit.validate();

            TSRange     raw_range = static_cast<TSRange>(*this);
            TSInputEdit raw_edit  = static_cast<TSInputEdit>(edit);

            ts_range_edit(&raw_range, &raw_edit);

            point.start = raw_range.start_point;
            point.end   = raw_range.end_point;
            byte.start  = raw_range.start_byte;
            byte.end    = raw_range.end_byte;
        }

        ////////////////////////////////////////////////////////////////
        // Operators
        ////////////////////////////////////////////////////////////////

        [[nodiscard]] operator TSRange() const noexcept
        {
            return { point.start, point.end, byte.start, byte.end };
        }
    };

    /////////////////////////////////////////////////////////////////////////////
    // Input
    // A structure that defines how to read and decode source code incrementally.
    /////////////////////////////////////////////////////////////////////////////

    struct Input
    {
        using ReadFunction = std::function<details::StringViewReturn(uint32_t, Point, uint32_t *)>;

        ReadFunction       read;
        InputEncoding      encoding;
        ts::DecodeFunction decode;

        Input() : impl(std::make_unique<TSInput>()), encoding(InputEncoding::UTF8), read(nullptr), decode(nullptr)
        {
            impl->payload = const_cast<Input *>(this);
            impl->read    = read_proxy;
        }

#if TS_HAS_CXX17
        static thread_local const Input *current_input_ptr;
#else
        static const Input *&current_ptr() noexcept
        {
            thread_local const Input *ptr = nullptr;
            return ptr;
        }
#endif

        ////////////////////////////////////////////////////////////////
        // Static Functions
        ////////////////////////////////////////////////////////////////

        [[nodiscard]] static const char *read_proxy(void     *payload,
                                                    uint32_t  byte_index,
                                                    TSPoint   position,
                                                    uint32_t *bytes_read)
        {
            auto *self = static_cast<Input *>(payload);
            if (self && self->read)
            {
                details::StringViewReturn result = self->read(byte_index, position, bytes_read);
                return result.data();
            }
            *bytes_read = 0;
            return nullptr;
        }

        [[nodiscard]] static uint32_t decode_proxy(const uint8_t *string, uint32_t length, int32_t *code_point) noexcept
        {
#if TS_HAS_CXX17
            if (current_input_ptr && current_input_ptr->decode)
            {
                // basic_string_view<uint8_t>, int32_t *
                return current_input_ptr->decode({ string, length }, code_point);
#else
            if (current_ptr() && current_ptr()->decode)
            {
                // const uint8_t*, uint32_t, int32_t *
                return current_ptr()->decode(string, length, code_point);
#endif
            }
            return 0;
        }

        ////////////////////////////////////////////////////////////////
        // Operators
        ////////////////////////////////////////////////////////////////

        [[nodiscard]] operator TSInput() const
        {
            TSInput c_input{};
            c_input.payload  = const_cast<Input *>(this);
            c_input.read     = read_proxy;
            c_input.encoding = static_cast<TSInputEncoding>(encoding);
            c_input.decode   = (decode) ? decode_proxy : nullptr;
            return c_input;
        }

        [[nodiscard]] operator TSInput *() const noexcept
        {
            impl->encoding = static_cast<TSInputEncoding>(encoding);
            impl->decode   = (decode) ? decode_proxy : nullptr;
            return impl.get();
        }

    private:
        std::unique_ptr<TSInput> impl;
    };

#if TS_HAS_CXX17
    inline thread_local const Input *Input::current_input_ptr = nullptr;
#endif

    /////////////////////////////////////////////////////////////////////////////
    // ParseState
    // A structure representing the state of the parser at a specific position.
    /////////////////////////////////////////////////////////////////////////////

    struct ParseState
    {
        uint32_t current_byte_offset;
        bool     has_error;

        explicit ParseState(const TSParseState *state) noexcept
            : current_byte_offset(state->current_byte_offset), has_error(state->has_error)
        {}

        explicit ParseState(const TSParseState &state) noexcept
            : current_byte_offset(state.current_byte_offset), has_error(state.has_error)
        {}
    };

    /////////////////////////////////////////////////////////////////////////////
    // ParseOptions
    // Options that control the behavior of the parsing process.
    /////////////////////////////////////////////////////////////////////////////

    using ParseOptions = details::OptionsBase<TSParseOptions, TSParseState, ParseState>;

    /////////////////////////////////////////////////////////////////////////////
    // Language
    // Represents a Tree-sitter grammar with metadata and symbol definitions.
    /////////////////////////////////////////////////////////////////////////////

    class Language
    {
    public:
        ////////////////////////////////////////////////////////////////
        // Lifecycle
        ////////////////////////////////////////////////////////////////

        // NOTE: Allowing implicit conversions from TSLanguage to Language
        // improves ergonomics for clients using external grammar providers.

        /* implicit */ Language(const TSLanguage *language) noexcept
            : impl{ language, [](const TSLanguage *l) { ts_language_delete(l); } }
        {
            is_valid = language != nullptr;
        }

        Language(const Language &other) noexcept
        {
            is_valid = other.is_valid;
            if (is_valid && other.impl.get() != nullptr)
            {
                impl = { ts_language_copy(other.impl.get()), [](const TSLanguage *l) { ts_language_delete(l); } };
            }
            else
            {
                is_valid = false;
                impl.reset();
            }
        }

        Language(Language &&other) noexcept = default;

        ////////////////////////////////////////////////////////////////
        // Count Accessors
        ////////////////////////////////////////////////////////////////

        [[nodiscard]] uint32_t getSymbolsCount() const noexcept
        {
            if (!is_valid)
            {
                return 0;
            }
            return ts_language_symbol_count(impl.get());
        }

        [[nodiscard]] uint32_t getStatesCount() const noexcept
        {
            if (!is_valid)
            {
                return 0;
            }
            return ts_language_state_count(impl.get());
        }

        [[nodiscard]] uint32_t getFieldsCount() const noexcept
        {
            if (!is_valid)
            {
                return 0;
            }
            return ts_language_field_count(impl.get());
        }

        ////////////////////////////////////////////////////////////////
        // Symbol Resolution
        ////////////////////////////////////////////////////////////////

        [[nodiscard]] details::StringViewReturn getSymbolName(Symbol symbol) const
        {
            if (!is_valid)
            {
                return details::StringViewReturn("");
            }
            return details::make_view(ts_language_symbol_name(impl.get(), symbol));
        }

        [[nodiscard]] SymbolType getSymbolType(Symbol symbol) const
        {
            if (!is_valid)
            {
                return SymbolType::TypeRegular;
            }
            return static_cast<SymbolType>(ts_language_symbol_type(impl.get(), symbol));
        }

        // Returns 0 if field was not found
        [[nodiscard]] Symbol getSymbolForName(details::StringViewParameter name, bool isNamed) const
        {
            if (!is_valid)
            {
                return 0;
            }

            if (name.size() > std::numeric_limits<uint32_t>::max())
            {
                throw std::length_error("Tree-sitter: Input name exceeds maximum size of 4GB");
            }

            return ts_language_symbol_for_name(impl.get(), name.data(), static_cast<uint32_t>(name.size()), isNamed);
        }

        ////////////////////////////////////////////////////////////////
        // Field Resolution
        ////////////////////////////////////////////////////////////////

        [[nodiscard]] details::StringViewReturn getFieldNameForID(FieldID id) const
        {
            if (!is_valid)
            {
                return details::StringViewReturn("");
            }
            return details::make_view(ts_language_field_name_for_id(impl.get(), id));
        }

        // Returns 0 if field was not found
        [[nodiscard]] FieldID getFieldIDForName(details::StringViewParameter name) const
        {
            if (!is_valid)
            {
                return 0;
            }

            if (name.size() > std::numeric_limits<uint32_t>::max())
            {
                throw std::length_error("Tree-sitter: Input name exceeds maximum size of 4GB");
            }

            return ts_language_field_id_for_name(impl.get(), name.data(), static_cast<uint32_t>(name.size()));
        }

        [[nodiscard]] std::vector<details::StringViewReturn> getAllFieldsNames() const
        {
            if (!is_valid)
            {
                return {};
            }

            const TSLanguage *raw_lang = impl.get();
            const uint32_t    count    = ts_language_field_count(raw_lang);

            if (count == 0)
            {
                return {};
            }

            std::vector<details::StringViewReturn> names;
            names.reserve(static_cast<size_t>(count));

            for (uint32_t i = 1; i <= count; ++i)
            {
                names.emplace_back(details::make_view(ts_language_field_name_for_id(raw_lang, i)));
            }

            return names;
        }

        ////////////////////////////////////////////////////////////////
        // Type Hierarchy
        ////////////////////////////////////////////////////////////////

        [[nodiscard]] std::vector<Symbol> getAllSuperTypes() const noexcept
        {
            if (!is_valid)
            {
                return {};
            }

            uint32_t        count = 0;
            const TSSymbol *array = ts_language_supertypes(impl.get(), &count);
            if (!array || count == 0)
            {
                return {};
            }

            std::vector<Symbol> vec(array, array + count);
            return vec;
        }

        [[nodiscard]] std::vector<Symbol> getAllSubTypesForSuperType(Symbol supertype) const noexcept
        {
            if (!is_valid)
            {
                return {};
            }

            uint32_t        count = 0;
            const TSSymbol *array = ts_language_subtypes(impl.get(), supertype, &count);
            if (!array || count == 0)
            {
                return {};
            }

            std::vector<Symbol> vec(array, array + count);
            return vec;
        }

        ////////////////////////////////////////////////////////////////
        // State Navigation
        ////////////////////////////////////////////////////////////////

        [[nodiscard]] StateID getNextState(StateID state, Symbol symbol) const noexcept
        {
            if (!is_valid)
            {
                return 0;
            }
            return ts_language_next_state(impl.get(), state, symbol);
        }

        [[nodiscard]] LookaheadIterator getLookaheadIterator(StateID state) const;

        ////////////////////////////////////////////////////////////////
        // Metadata
        ////////////////////////////////////////////////////////////////

        [[nodiscard]] details::StringViewReturn getName() const
        {
            if (!is_valid)
            {
                return details::StringViewReturn("");
            }
            return details::make_view(ts_language_name(impl.get()));
        }

        [[nodiscard]] Version getVersion() const noexcept
        {
            if (!is_valid)
            {
                return 0;
            }
            return ts_language_abi_version(impl.get());
        }

        [[nodiscard]] bool isValid() const noexcept
        {
            return is_valid;
        }

#if TS_HAS_CXX17
        [[nodiscard]] std::optional<LanguageMetadata> getMetadata() const noexcept
#else
        [[nodiscard]] LanguageMetadata getMetadata() const noexcept
#endif
        {
            if (!is_valid)
            {
#if TS_HAS_CXX17
                return std::nullopt;
#else
                return { 0, 0, 0 };
#endif
            }
            const TSLanguageMetadata *metadata = ts_language_metadata(impl.get());
            if (!metadata)
            {
#if TS_HAS_CXX17
                return std::nullopt;
#else
                return { 0, 0, 0 };
#endif
            }
            return LanguageMetadata{ metadata->major_version, metadata->minor_version, metadata->patch_version };
        }

#if defined(CPP_TREE_SITTER_FEATURE_WASM)
        [[nodiscard]] bool isWasm() const noexcept
        {
            if (!is_valid)
            {
                return false;
            }
            return ts_language_is_wasm(impl.get());
        }
#endif

        ////////////////////////////////////////////////////////////////
        // Operators
        ////////////////////////////////////////////////////////////////

        Language &operator=(const Language &other) noexcept
        {
            if (this != &other)
            {
                is_valid = other.is_valid;
                if (is_valid && other.impl.get() != nullptr)
                {
                    impl = { ts_language_copy(other.impl.get()), [](const TSLanguage *l) { ts_language_delete(l); } };
                }
                else
                {
                    is_valid = false;
                    impl.reset();
                }
            }
            return *this;
        }

        Language &operator=(Language &&other) noexcept = default;

        [[nodiscard]] operator const TSLanguage *() const noexcept
        {
            return impl.get();
        }

    private:
        std::shared_ptr<const TSLanguage> impl;
        bool                              is_valid = false;
    };

    /////////////////////////////////////////////////////////////////////////////
    // Node
    // A single node in a parse tree, providing navigation and attributes.
    /////////////////////////////////////////////////////////////////////////////

    struct Node
    {
        ////////////////////////////////////////////////////////////////
        // Lifecycle
        ////////////////////////////////////////////////////////////////

        Node() noexcept : impl{ TSNode{} }
        {}

        explicit Node(TSNode node) noexcept : impl{ node }
        {}

        [[nodiscard]] static Node null() noexcept
        {
            return Node();
        }

        ////////////////////////////////////////////////////////////////
        // Identification & Type
        ////////////////////////////////////////////////////////////////

        // Returns a unique identifier for a node in a parse tree.
        [[nodiscard]] NodeID getID() const noexcept
        {
            return isNull() ? 0 : reinterpret_cast<NodeID>(impl.id);
        }

        [[nodiscard]] details::StringViewReturn getType() const
        {
            return isNull() ? details::StringViewReturn("") : details::make_view(ts_node_type(impl));
        }

        [[nodiscard]] Symbol getSymbol() const noexcept
        {
            return isNull() ? 0 : ts_node_symbol(impl);
        }

        [[nodiscard]] details::StringViewReturn getGrammarType() const
        {
            return isNull() ? details::StringViewReturn("") : details::make_view(ts_node_grammar_type(impl));
        }

        [[nodiscard]] Symbol getGrammarSymbol() const noexcept
        {
            return isNull() ? 0 : ts_node_grammar_symbol(impl);
        }

        [[nodiscard]] Language getLanguage() const noexcept
        {
            return isNull() ? Language{ nullptr } : Language{ ts_node_language(impl) };
        }

        ////////////////////////////////////////////////////////////////
        // Flag Checks
        ////////////////////////////////////////////////////////////////

        [[nodiscard]] bool isNull() const noexcept
        {
            return ts_node_is_null(impl);
        }

        [[nodiscard]] bool isNamed() const noexcept
        {
            return !isNull() && ts_node_is_named(impl);
        }

        [[nodiscard]] bool isMissing() const noexcept
        {
            return !isNull() && ts_node_is_missing(impl);
        }

        [[nodiscard]] bool isExtra() const noexcept
        {
            return !isNull() && ts_node_is_extra(impl);
        }

        [[nodiscard]] bool isError() const noexcept
        {
            return !isNull() && ts_node_is_error(impl);
        }

        [[nodiscard]] bool hasError() const noexcept
        {
            return !isNull() && ts_node_has_error(impl);
        }

        [[nodiscard]] bool hasChanges() const noexcept
        {
            return !isNull() && ts_node_has_changes(impl);
        }

        ////////////////////////////////////////////////////////////////
        // Ranges
        ////////////////////////////////////////////////////////////////

        [[nodiscard]] Extent<uint32_t> getByteRange() const noexcept
        {
            return isNull() ? Extent<uint32_t>()
                            : Extent<uint32_t>({ ts_node_start_byte(impl), ts_node_end_byte(impl) });
        }

        [[nodiscard]] Extent<Point> getPointRange() const noexcept
        {
            return isNull() ? Extent<Point>() : Extent<Point>({ ts_node_start_point(impl), ts_node_end_point(impl) });
        }

        ////////////////////////////////////////////////////////////////
        // Source Text Access
        ////////////////////////////////////////////////////////////////

        [[nodiscard]] details::StringViewReturn getSourceRange(details::StringViewParameter source) const
        {
            if (isNull())
            {
                return {};
            }

            Extent<uint32_t> extents = getByteRange();
            size_t           end     = static_cast<size_t>(extents.end);
            if (end > source.size())
            {
                return "";
            }
            size_t start = static_cast<size_t>(extents.start);
            return source.substr(start, end - start);
        }

        [[nodiscard]] std::string getSourceText(details::StringViewParameter source) const
        {
            return isNull() ? "" : std::string(getSourceRange(source));
        }

        // Returns an S-Expression representation of the subtree rooted at this node.
        [[nodiscard]] std::unique_ptr<char, details::FreeHelper> getSExpr() const
        {
            if (isNull())
            {
                return nullptr;
            }
            return std::unique_ptr<char, details::FreeHelper>{ ts_node_string(impl) };
        }

        ////////////////////////////////////////////////////////////////
        // Navigation (General)
        ////////////////////////////////////////////////////////////////

        [[nodiscard]] Node getParent() const noexcept
        {
            return isNull() ? null() : Node{ ts_node_parent(impl) };
        }

        [[nodiscard]] Node getNextSibling() const noexcept
        {
            return isNull() ? null() : Node{ ts_node_next_sibling(impl) };
        }

        [[nodiscard]] Node getPreviousSibling() const noexcept
        {
            return isNull() ? null() : Node{ ts_node_prev_sibling(impl) };
        }

        [[nodiscard]] uint32_t getChildCount() const noexcept
        {
            return isNull() ? 0 : ts_node_child_count(impl);
        }

        [[nodiscard]] Node getChild(uint32_t child_index) const noexcept
        {
            if (child_index >= getChildCount())
            {
                return null();
            }

            return Node{ ts_node_child(impl, child_index) };
        }

        [[nodiscard]] Node getFirstChild() const noexcept
        {
            return getChild(0);
        }

        [[nodiscard]] Node getLastChild() const noexcept
        {
            uint32_t count = getChildCount();

            return getChild(count > 0 ? count - 1 : count);
        }

        [[nodiscard]] Node getChildWithDescendant(Node descendant) const noexcept
        {
            if (isNull() || descendant.isNull())
            {
                return null();
            }
            return Node{ ts_node_child_with_descendant(impl, descendant.impl) };
        }

        [[nodiscard]] Node getChildByType(details::StringViewParameter name) const
        {
            if (name.size() > std::numeric_limits<uint32_t>::max())
            {
                throw std::length_error("Tree-sitter: Input name exceeds maximum size of 4GB");
            }

            if (isNull())
            {
                return null();
            }

            Language lang    = getLanguage();
            bool     isNamed = true;
            Symbol   sym     = lang.getSymbolForName(name, isNamed);

            if (sym == 0)
            {
                isNamed = false;
                sym     = lang.getSymbolForName(name, isNamed);
            }

            if (sym == 0)
            {
                return null();
            }

            if (isNamed)
            {
                for (Node child = getFirstNamedChild(); !child.isNull(); child = child.getNextNamedSibling())
                {
                    if (child.getSymbol() == sym)
                    {
                        return child;
                    }
                }
            }
            else
            {
                for (Node child = getFirstChild(); !child.isNull(); child = child.getNextSibling())
                {
                    if (child.getSymbol() == sym)
                    {
                        return child;
                    }
                }
            }

            return null();
        }

        [[nodiscard]] Node getChildBySymbol(Symbol sym) const noexcept
        {
            if (isNull())
            {
                return null();
            }

            for (Node child = getFirstChild(); !child.isNull(); child = child.getNextSibling())
            {
                if (child.getSymbol() == sym)
                {
                    return child;
                }
            }

            return null();
        }

        ////////////////////////////////////////////////////////////////
        // Navigation (Named Children)
        ////////////////////////////////////////////////////////////////

        [[nodiscard]] Node getNextNamedSibling() const noexcept
        {
            return isNull() ? null() : Node{ ts_node_next_named_sibling(impl) };
        }

        [[nodiscard]] Node getPreviousNamedSibling() const noexcept
        {
            return isNull() ? null() : Node{ ts_node_prev_named_sibling(impl) };
        }

        [[nodiscard]] uint32_t getNamedChildCount() const noexcept
        {
            return isNull() ? 0 : ts_node_named_child_count(impl);
        }

        [[nodiscard]] Node getNamedChild(uint32_t child_index) const noexcept
        {
            if (child_index >= getNamedChildCount())
            {
                return null();
            }

            return Node{ ts_node_named_child(impl, child_index) };
        }

        [[nodiscard]] Node getFirstNamedChild() const noexcept
        {
            return getNamedChild(0);
        }

        [[nodiscard]] Node getLastNamedChild() const noexcept
        {
            uint32_t count = getNamedChildCount();

            return getNamedChild(count > 0 ? count - 1 : count);
        }

        [[nodiscard]] Node getNamedChildByType(details::StringViewParameter name) const
        {
            if (name.size() > std::numeric_limits<uint32_t>::max())
            {
                throw std::length_error("Tree-sitter: Input name exceeds maximum size of 4GB");
            }

            if (isNull())
            {
                return null();
            }

            Language lang = getLanguage();
            Symbol   sym  = lang.getSymbolForName(name, true);

            if (sym == 0)
            {
                return null();
            }

            for (Node child = getFirstNamedChild(); !child.isNull(); child = child.getNextNamedSibling())
            {
                if (child.getSymbol() == sym)
                {
                    return child;
                }
            }

            return null();
        }

        [[nodiscard]] Node getNamedChildBySymbol(Symbol sym) const noexcept
        {
            if (isNull())
            {
                return null();
            }

            for (Node child = getFirstNamedChild(); !child.isNull(); child = child.getNextNamedSibling())
            {
                if (child.getSymbol() == sym)
                {
                    return child;
                }
            }

            return null();
        }

        ////////////////////////////////////////////////////////////////
        // Search & Descendants
        ////////////////////////////////////////////////////////////////

        [[nodiscard]] uint32_t getDescendantsCount() const noexcept
        {
            return isNull() ? 0 : ts_node_descendant_count(impl);
        }

        [[nodiscard]] Node getFirstChildForByte(uint32_t byte) const noexcept
        {
            return isNull() ? null() : Node{ ts_node_first_child_for_byte(impl, byte) };
        }

        [[nodiscard]] Node getFirstNamedChildForByte(uint32_t byte) const noexcept
        {
            return isNull() ? null() : Node{ ts_node_first_named_child_for_byte(impl, byte) };
        }

        [[nodiscard]] Node getDescendantForByteRange(Extent<uint32_t> range) const noexcept
        {
            return isNull() ? null() : Node{ ts_node_descendant_for_byte_range(impl, range.start, range.end) };
        }

        [[nodiscard]] Node getNamedDescendantForByteRange(Extent<uint32_t> range) const noexcept
        {
            return isNull() ? null() : Node{ ts_node_named_descendant_for_byte_range(impl, range.start, range.end) };
        }

        [[nodiscard]] Node getDescendantForPointRange(Extent<Point> range) const noexcept
        {
            return isNull() ? null() : Node{ ts_node_descendant_for_point_range(impl, range.start, range.end) };
        }

        [[nodiscard]] Node getNamedDescendantForPointRange(Extent<Point> range) const noexcept
        {
            return isNull() ? null() : Node{ ts_node_named_descendant_for_point_range(impl, range.start, range.end) };
        }

        ////////////////////////////////////////////////////////////////
        // Fields
        ////////////////////////////////////////////////////////////////

        [[nodiscard]] details::StringViewReturn getFieldNameForChild(uint32_t child_index) const
        {
            return isNull() ? details::StringViewReturn("")
                            : details::make_view(ts_node_field_name_for_child(impl, child_index));
        }

        [[nodiscard]] details::StringViewReturn getFieldNameForNamedChild(uint32_t named_child_index) const
        {
            return isNull() ? details::StringViewReturn("")
                            : details::make_view(ts_node_field_name_for_named_child(impl, named_child_index));
        }

        [[nodiscard]] Node getChildByFieldName(details::StringViewParameter name) const
        {
            if (name.size() > std::numeric_limits<uint32_t>::max())
            {
                throw std::length_error("Tree-sitter: Input name exceeds maximum size of 4GB");
            }

            return isNull()
                         ? null()
                         : Node{ ts_node_child_by_field_name(impl, name.data(), static_cast<uint32_t>(name.size())) };
        }

        [[nodiscard]] Node getChildByFieldID(FieldID field_id) const noexcept
        {
            return isNull() ? null() : Node{ ts_node_child_by_field_id(impl, field_id) };
        }

        ////////////////////////////////////////////////////////////////
        // Parsing State
        ////////////////////////////////////////////////////////////////

        [[nodiscard]] StateID getParseState() const noexcept
        {
            return isNull() ? 0 : ts_node_parse_state(impl);
        }

        [[nodiscard]] StateID getNextParseState() const noexcept
        {
            return isNull() ? 0 : ts_node_next_parse_state(impl);
        }

        ////////////////////////////////////////////////////////////////
        // Mutations
        ////////////////////////////////////////////////////////////////

        void edit(const InputEdit &edit)
        {
            if (isNull())
            {
                throw std::logic_error("Tree-sitter: Couldn't change null Node");
            }

            edit.validate();

            TSInputEdit raw_edit = static_cast<TSInputEdit>(edit);

            ts_node_edit(&impl, &raw_edit);
        }

        ////////////////////////////////////////////////////////////////
        // Cursor
        ////////////////////////////////////////////////////////////////

        // Definition deferred until after the definition of TreeCursor.
        [[nodiscard]] TreeCursor getCursor() const noexcept;

        ////////////////////////////////////////////////////////////////
        // Comparison
        ////////////////////////////////////////////////////////////////

        [[nodiscard]] bool operator==(const Node &other) const noexcept
        {
            return ts_node_eq(impl, other.impl);
        }

#if !TS_HAS_CXX20
        [[nodiscard]] bool operator!=(const Node &other) const noexcept
        {
            return !(*this == other);
        }
#endif

        TSNode impl;
    };

    /////////////////////////////////////////////////////////////////////////////
    // Tree
    // Manages the lifetime of a complete parse tree.
    /////////////////////////////////////////////////////////////////////////////

    class Tree
    {
    public:
        ////////////////////////////////////////////////////////////////
        // Lifecycle
        ////////////////////////////////////////////////////////////////

        explicit Tree(TSTree *tree) noexcept : impl{ tree, ts_tree_delete }
        {
            is_valid = tree != nullptr;
        }

        Tree(const Tree &other) noexcept
        {
            is_valid = other.is_valid;
            if (is_valid && other.impl.get() != nullptr)
            {
                impl.reset(ts_tree_copy(other.impl.get()));
            }
            else
            {
                is_valid = false;
                impl.reset();
            }
        }

        Tree(Tree &&other) noexcept = default;

        ////////////////////////////////////////////////////////////////
        // Flags
        ////////////////////////////////////////////////////////////////

        [[nodiscard]] bool hasError() const noexcept
        {
            return is_valid && getRootNode().hasError();
        }

        [[nodiscard]] bool isValid() const noexcept
        {
            return is_valid;
        }

        ////////////////////////////////////////////////////////////////
        // Root
        ////////////////////////////////////////////////////////////////

        [[nodiscard]] Node getRootNode() const noexcept
        {
            if (!is_valid)
            {
                return Node::null();
            }
            return Node{ ts_tree_root_node(impl.get()) };
        }

        [[nodiscard]] Node getRootNodeWithOffset(uint32_t offset_bytes, Point offset_extent) const noexcept
        {
            if (!is_valid)
            {
                return Node::null();
            }
            return Node{ ts_tree_root_node_with_offset(impl.get(), offset_bytes, offset_extent) };
        }

        void edit(const InputEdit &edit)
        {
            if (!is_valid)
            {
                throw std::logic_error("Tree-sitter: Couldn't change invalid Tree");
            }

            edit.validate();

            TSInputEdit raw_edit = static_cast<TSInputEdit>(edit);

            ts_tree_edit(impl.get(), &raw_edit);
        }

        ////////////////////////////////////////////////////////////////
        // Ranges
        ////////////////////////////////////////////////////////////////

        [[nodiscard]] std::vector<Range> getIncludedRanges() const
        {
            if (!is_valid)
            {
                return {};
            }

            uint32_t count = 0;
            TSRange *array = ts_tree_included_ranges(impl.get(), &count);
            if (!array)
            {
                return {};
            }

            if (count == 0)
            {
                std::free(array);
                return {};
            }

            std::vector<Range> vec;
            vec.reserve(count);
            for (uint32_t i = 0; i < count; ++i)
            {
                vec.emplace_back(array[i]);
            }

            std::free(array);
            return vec;
        }

        [[nodiscard]] static std::vector<Range> getChangedRanges(const Tree &old_tree, const Tree &new_tree)
        {
            if (!old_tree.isValid() || !new_tree.isValid())
            {
                return {};
            }

            uint32_t count = 0;
            TSRange *array = ts_tree_get_changed_ranges(static_cast<const TSTree *>(old_tree),
                                                        static_cast<const TSTree *>(new_tree),
                                                        &count);
            if (!array)
            {
                return {};
            }

            if (count == 0)
            {
                std::free(array);
                return {};
            }

            std::vector<Range> vec;
            vec.reserve(count);
            for (uint32_t i = 0; i < count; ++i)
            {
                vec.emplace_back(array[i]);
            }

            std::free(array);
            return vec;
        }

        ////////////////////////////////////////////////////////////////
        // Language
        ////////////////////////////////////////////////////////////////

        [[nodiscard]] Language getLanguage() const noexcept
        {
            if (!is_valid)
            {
                return Language{ nullptr };
            }
            return Language{ ts_tree_language(impl.get()) };
        }

        ////////////////////////////////////////////////////////////////
        // Copy
        ////////////////////////////////////////////////////////////////

        [[nodiscard]] Tree copy() const noexcept
        {
            return Tree(*this);
        }

        ////////////////////////////////////////////////////////////////
        // Debugging
        ////////////////////////////////////////////////////////////////

        void printDotGraph(int file_descriptor) noexcept
        {
            if (!is_valid)
            {
                return;
            }
            ts_tree_print_dot_graph(impl.get(), file_descriptor);
        }

        void printDotGraph(FILE *file)
        {
            if (!is_valid || file == nullptr)
            {
                return;
            }

            printDotGraph(TS_FILENO(file));
        }

        void printDotGraph(details::StringViewParameter path)
        {
            if (!is_valid)
            {
                return;
            }

            std::string path_str(path.data(), path.size());

#if defined(_WIN32)
            FILE *file = nullptr;
            fopen_s(&file, path_str.c_str(), "w");
#else
            FILE *file = fopen(path_str.c_str(), "w");
#endif
            printDotGraph(file);

            // Close local file descriptor after write
            if (file != nullptr)
            {
                fclose(file);
            }
        }

        ////////////////////////////////////////////////////////////////
        // Operators
        ////////////////////////////////////////////////////////////////

        Tree &operator=(const Tree &other)
        {
            if (this != &other)
            {
                is_valid = other.is_valid;
                if (is_valid && other.impl.get() != nullptr)
                {
                    impl.reset(ts_tree_copy(other.impl.get()));
                }
                else
                {
                    is_valid = false;
                    impl.reset();
                }
            }
            return *this;
        }

        Tree &operator=(Tree &&other) noexcept = default;

        [[nodiscard]] operator TSTree *() const noexcept
        {
            return impl.get();
        }

    private:
        std::unique_ptr<TSTree, decltype(&ts_tree_delete)> impl     = { nullptr, ts_tree_delete };
        bool                                               is_valid = false;
    };

    /////////////////////////////////////////////////////////////////////////////
    // Parser
    // State machine used to produce a syntax tree from source code.
    /////////////////////////////////////////////////////////////////////////////

    class Parser
    {
    public:
        ////////////////////////////////////////////////////////////////
        // Aliases
        ////////////////////////////////////////////////////////////////

        using LoggerFunction = std::function<void(LogType, const char *)>;

        ////////////////////////////////////////////////////////////////
        // Lifecycle
        ////////////////////////////////////////////////////////////////

        Parser() noexcept : impl{ ts_parser_new(), ts_parser_delete }
        {}

        Parser(Language language) : impl{ ts_parser_new(), ts_parser_delete }
        {
            if (!language.isValid())
            {
                throw std::runtime_error("Tree-sitter: Passed Language for Parser was invalid");
            }

#if TS_HAS_CXX23
            if (auto res = setLanguage(language); !res.has_value())
            {
                throw std::runtime_error(res.error());
            }
#else
            if (!setLanguage(language))
            {
                throw std::runtime_error("Tree-sitter: Couldn't set language (version mismatch)");
            }
#endif
        }

#if defined(CPP_TREE_SITTER_FEATURE_WASM)
        Parser(Language language, WasmStore &store) : impl{ ts_parser_new(), ts_parser_delete }
        {
            if (!language.isValid())
            {
                throw std::runtime_error("Tree-sitter: Passed Language for Parser was invalid");
            }

            setWasmStore(store);

#if TS_HAS_CXX23
            if (auto res = setLanguage(language); !res.has_value())
            {
                throw std::runtime_error(res.error());
            }
#else
            if (!setLanguage(language))
            {
                throw std::runtime_error("Tree-sitter: Couldn't set language (version mismatch)");
            }
#endif
        }
#endif

#if TS_HAS_CXX23
        [[nodiscard]] static inline std::expected<Parser, std::string> create(Language language) noexcept
        {
            if (!language.isValid())
            {
                return std::unexpected("Tree-sitter: Passed Language for Parser was invalid");
            }

            Parser parser;
            if (auto res = parser.setLanguage(language); !res.has_value())
            {
                return std::unexpected(res.error());
            }

            return parser;
        }

#if defined(CPP_TREE_SITTER_FEATURE_WASM)
        [[nodiscard]] static inline std::expected<Parser, std::string> create(Language   language,
                                                                              WasmStore &store) noexcept;
#endif
#endif

        ////////////////////////////////////////////////////////////////
        // Configuration
        ////////////////////////////////////////////////////////////////

        [[nodiscard]]
#if TS_HAS_CXX23
        std::expected<bool, std::string> setLanguage(Language language)
        {
#if defined(CPP_TREE_SITTER_FEATURE_WASM)
            if (language.isWasm() && !wasm_store_set)
            {
                return std::unexpected("Tree-sitter: Parser needs WasmStore to process Wasm Language");
            }
#endif
            language_set = ts_parser_set_language(impl.get(), language);
            if (!language_set)
            {
                return std::unexpected("Tree-sitter: Language version mismatch (ABI incompatibility)");
            }
#else
        bool setLanguage(Language language)
#if defined(CPP_TREE_SITTER_FEATURE_WASM)
        {
            if (language.isWasm() && !wasm_store_set)
            {
                throw std::logic_error("Tree-sitter: Parser needs WasmStore to process Wasm Language");
            }

#else
                noexcept
        {
#endif
            language_set = ts_parser_set_language(impl.get(), language);
#endif
            return language_set;
        }

        [[nodiscard]]
#if TS_HAS_CXX20
        bool setIncludedRanges(std::span<const Range> ranges)
#else
        bool setIncludedRanges(const std::vector<Range> &ranges)
#endif
        {
            if (ranges.empty())
            {
                return ts_parser_set_included_ranges(impl.get(), nullptr, 0);
            }

            std::vector<TSRange> c_ranges;
            c_ranges.reserve(ranges.size());
            for (const auto &r : ranges)
            {
                c_ranges.push_back(static_cast<TSRange>(r));
            }

            std::sort(c_ranges.begin(),
                      c_ranges.end(),
                      [](const TSRange &a, const TSRange &b) { return a.start_byte < b.start_byte; });

            std::vector<TSRange> merged;
            merged.reserve(c_ranges.size());
            merged.push_back(c_ranges[0]);

            for (size_t i = 1; i < c_ranges.size(); ++i)
            {
                TSRange &last    = merged.back();
                TSRange &current = c_ranges[i];

                // If ranges overlap
                if (current.start_byte <= last.end_byte)
                {
                    // Update end_byte (futher from two)
                    if (current.end_byte > last.end_byte)
                    {
                        last.end_byte  = current.end_byte;
                        last.end_point = current.end_point;
                    }
                }
                else
                {
                    merged.push_back(current);
                }
            }

            if (merged.size() > std::numeric_limits<uint32_t>::max())
            {
                throw std::length_error(
                        "Tree-sitter: Ranges size exceeds maximum size. (Max size is equal to UINT32_MAX value)");
            }

            return ts_parser_set_included_ranges(impl.get(), merged.data(), static_cast<uint32_t>(merged.size()));
        }

#if defined(CPP_TREE_SITTER_FEATURE_WASM)
        // Passing a WasmStore invalidates it. The parser takes control of the WasmStore. You can retrieve it using the
        // takeWasmStore() function. If you set a new WasmStore without retrieving the previous one, the previous
        // instance will be discarded.
        void setWasmStore(WasmStore &store) noexcept;
#endif

        [[nodiscard]] Language getCurrentLanguage() const noexcept
        {
            if (!language_set)
            {
                return Language{ nullptr };
            }
            return Language{ ts_parser_language(impl.get()) };
        }

        [[nodiscard]] std::vector<Range> getIncludedRanges() const noexcept
        {
            uint32_t       count = 0;
            const TSRange *array = ts_parser_included_ranges(impl.get(), &count);
            if (!array || count == 0)
            {
                return {};
            }

            std::vector<Range> vec;
            vec.reserve(count);
            for (uint32_t i = 0; i < count; ++i)
            {
                vec.emplace_back(array[i]);
            }

            return vec;
        }

#if defined(CPP_TREE_SITTER_FEATURE_WASM)
        // If the set language is WebAssembly, it will be removed from the parser.
        [[nodiscard]] WasmStore takeWasmStore() noexcept;
#endif

        ////////////////////////////////////////////////////////////////
        // Flags
        ////////////////////////////////////////////////////////////////

        [[nodiscard]] bool hasLanguage() const noexcept
        {
            return language_set;
        }

#if defined(CPP_TREE_SITTER_FEATURE_WASM)
        [[nodiscard]] bool hasWasmStore() const noexcept
        {
            return wasm_store_set;
        }
#endif

        ////////////////////////////////////////////////////////////////
        // Parsing
        ////////////////////////////////////////////////////////////////

        [[nodiscard]] Tree parse(Input                                     &input,
                                 details::OptionalParam<Tree>               old_tree = {},
                                 details::OptionalParam<const ParseOptions> options  = {})
        {
            if (!language_set)
            {
                throw std::logic_error("Tree-sitter: Cannot parse without a language. Use setLanguage() first.");
            }

#if TS_HAS_CXX17
            Input::current_input_ptr = &input;
#else
            Input::current_ptr() = &input;
#endif

            TSTree               *raw_old_tree = details::get_raw<Tree, TSTree>(old_tree);
            const TSParseOptions *raw_options  = details::get_raw<const ParseOptions, const TSParseOptions>(options);

            TSTree *new_tree = nullptr;
            if (raw_options)
            {
                new_tree = ts_parser_parse_with_options(impl.get(), raw_old_tree, input, *raw_options);
            }
            else
            {
                new_tree = ts_parser_parse(impl.get(), raw_old_tree, input);
            }

#if TS_HAS_CXX17
            Input::current_input_ptr = nullptr;
#else
            Input::current_ptr() = nullptr;
#endif
            return Tree(new_tree);
        }

        [[nodiscard]] Tree parseString(details::StringViewParameter buffer, details::OptionalParam<Tree> old_tree = {})
        {
            if (!language_set)
            {
                throw std::logic_error("Tree-sitter: Cannot parse without a language. Use setLanguage() first.");
            }

            if (buffer.size() > std::numeric_limits<uint32_t>::max())
            {
                throw std::length_error("Tree-sitter: Input buffer exceeds maximum size of 4GB");
            }

            TSTree *raw_old_tree = details::get_raw<Tree, TSTree>(old_tree);

            return Tree{
                ts_parser_parse_string(impl.get(), raw_old_tree, buffer.data(), static_cast<uint32_t>(buffer.size()))
            };
        }

        [[nodiscard]] Tree parseStringEncoded(details::StringViewParameter buffer,
                                              InputEncoding                encoding,
                                              details::OptionalParam<Tree> old_tree = {})
        {
            if (!language_set)
            {
                throw std::logic_error("Tree-sitter: Cannot parse without a language. Use setLanguage() first.");
            }

            if (buffer.size() > std::numeric_limits<uint32_t>::max())
            {
                throw std::length_error("Tree-sitter: Input buffer exceeds maximum size of 4GB");
            }

            TSTree *raw_old_tree = details::get_raw<Tree, TSTree>(old_tree);

            return Tree{ ts_parser_parse_string_encoding(impl.get(),
                                                         raw_old_tree,
                                                         buffer.data(),
                                                         static_cast<uint32_t>(buffer.size()),
                                                         static_cast<TSInputEncoding>(encoding)) };
        }

        ////////////////////////////////////////////////////////////////
        // Debugging
        ////////////////////////////////////////////////////////////////

        void enableDotGraphs(int file_descriptor)
        {
            if (file_descriptor >= 0)
            {
                // Creates duplicate of user descriptor
                int duplicated_fd = TS_DUP(file_descriptor);

                if (duplicated_fd == -1)
                {
                    throw std::runtime_error("Tree-sitter: Failed to duplicate file descriptor for DOT graphs");
                }

                ts_parser_print_dot_graphs(impl.get(), duplicated_fd);
            }
            else
            {
                ts_parser_print_dot_graphs(impl.get(), -1);
            }
        }

        void enableDotGraphs(FILE *file)
        {
            if (file)
            {
                enableDotGraphs(TS_FILENO(file));
            }
        }

        void enableDotGraphs(details::StringViewParameter path)
        {
            std::string path_str(path.data(), path.size());

#if defined(_WIN32)
            FILE *file = nullptr;
            fopen_s(&file, path_str.c_str(), "w");
#else
            FILE *file = fopen(path_str.c_str(), "w");
#endif
            enableDotGraphs(file);

            // Close local file descriptor
            if (file != nullptr)
            {
                fclose(file);
            }
        }

        void disableDotGraphs()
        {
            enableDotGraphs(-1);
        }

        ////////////////////////////////////////////////////////////////
        // Logging
        ////////////////////////////////////////////////////////////////

        void setLogger(LoggerFunction logger_func)
        {
            current_logger = std::move(logger_func);

            TSLogger ts_logger{};
            ts_logger.payload = this;
            ts_logger.log     = [](void *payload, TSLogType log_type, const char *buffer)
            {
                if (payload)
                {
                    auto *self = static_cast<Parser *>(payload);
                    if (self->current_logger)
                    {
                        self->current_logger(static_cast<LogType>(log_type), buffer);
                    }
                }
            };

            ts_parser_set_logger(impl.get(), ts_logger);
        }

        void removeLogger() noexcept
        {
            current_logger = nullptr;
            ts_parser_set_logger(impl.get(), { nullptr, nullptr });
        }

        ////////////////////////////////////////////////////////////////
        // Reset
        ////////////////////////////////////////////////////////////////

        // Does not remove logger
        void reset() noexcept
        {
            ts_parser_reset(impl.get());
        }

    private:
        std::unique_ptr<TSParser, decltype(&ts_parser_delete)> impl;
        std::function<void(LogType, const char *)>             current_logger;
        bool                                                   language_set = false;
#if defined(CPP_TS_TEST_FEATURE_WASM)
        bool wasm_store_set = false;
#endif
    };

    /////////////////////////////////////////////////////////////////////////////
    // TreeCursor
    // A stateful pointer for walking a syntax tree efficiently.
    /////////////////////////////////////////////////////////////////////////////

    class TreeCursor
    {
    public:
        ////////////////////////////////////////////////////////////////
        // Lifecycle
        ////////////////////////////////////////////////////////////////

        TreeCursor() noexcept : is_valid(false), impl({})
        {}

        explicit TreeCursor(TSNode node) noexcept
        {
            is_valid = !ts_node_is_null(node);
            if (is_valid)
            {
                impl = ts_tree_cursor_new(node);
            }
            else
            {
                impl = {};
            }
        }

        TreeCursor(const TSTreeCursor &cursor) noexcept : impl{ ts_tree_cursor_copy(&cursor) }
        {
            TSNode node = ts_tree_cursor_current_node(&impl);
            is_valid    = !ts_node_is_null(node);
        }

        // By default avoid copies until the ergonomics are clearer.
        TreeCursor(const TreeCursor &other) = delete;

        TreeCursor(TreeCursor &&other) noexcept : impl{}
        {
            std::swap(impl, other.impl);
            std::swap(is_valid, other.is_valid);
        }

        ~TreeCursor() noexcept
        {
            if (!is_valid)
            {
                return;
            }
            ts_tree_cursor_delete(&impl);
        }

        ////////////////////////////////////////////////////////////////
        // Flags
        ////////////////////////////////////////////////////////////////

        [[nodiscard]] bool isValid() const noexcept
        {
            return is_valid;
        }

        ////////////////////////////////////////////////////////////////
        // State Control
        ////////////////////////////////////////////////////////////////

        void reset(Node node) noexcept
        {
            if (!is_valid || node.isNull())
            {
                return;
            }
            ts_tree_cursor_reset(&impl, node.impl);
        }

        void reset(TreeCursor &cursor) noexcept
        {
            if (!is_valid || !cursor.isValid())
            {
                return;
            }
            ts_tree_cursor_reset_to(&impl, &cursor.impl);
        }

        [[nodiscard]] TreeCursor copy() const noexcept
        {
            return TreeCursor(impl);
        }

        ////////////////////////////////////////////////////////////////
        // Navigation
        ////////////////////////////////////////////////////////////////

        [[nodiscard]] bool gotoParent() noexcept
        {
            if (!is_valid)
            {
                return false;
            }
            return ts_tree_cursor_goto_parent(&impl);
        }

        [[nodiscard]] bool gotoFirstChild() noexcept
        {
            if (!is_valid)
            {
                return false;
            }
            return ts_tree_cursor_goto_first_child(&impl);
        }

        [[nodiscard]] bool gotoLastChild() noexcept
        {
            if (!is_valid)
            {
                return false;
            }
            return ts_tree_cursor_goto_last_child(&impl);
        }

        [[nodiscard]] bool gotoNextSibling() noexcept
        {
            if (!is_valid)
            {
                return false;
            }
            return ts_tree_cursor_goto_next_sibling(&impl);
        }

        [[nodiscard]] bool gotoPreviousSibling() noexcept
        {
            if (!is_valid)
            {
                return false;
            }
            return ts_tree_cursor_goto_previous_sibling(&impl);
        }

        [[nodiscard]] int64_t gotoFirstChildForByte(uint32_t byte) noexcept
        {
            if (!is_valid)
            {
                return -1;
            }
            return ts_tree_cursor_goto_first_child_for_byte(&impl, byte);
        }

        [[nodiscard]] int64_t gotoFirstChildForPoint(Point point) noexcept
        {
            if (!is_valid)
            {
                return -1;
            }
            return ts_tree_cursor_goto_first_child_for_point(&impl, point);
        }

        void gotoDescendant(uint32_t descendant_index) noexcept
        {
            if (!is_valid)
            {
                return;
            }
            ts_tree_cursor_goto_descendant(&impl, descendant_index);
        }

        ////////////////////////////////////////////////////////////////
        // Current Node Attributes
        ////////////////////////////////////////////////////////////////

        [[nodiscard]] Node getCurrentNode() const noexcept
        {
            if (!is_valid)
            {
                return Node::null();
            }
            return Node{ ts_tree_cursor_current_node(&impl) };
        }

        [[nodiscard]] details::StringViewReturn getCurrentFieldName() const
        {
            if (!is_valid)
            {
                return details::StringViewReturn("");
            }
            return details::make_view(ts_tree_cursor_current_field_name(&impl));
        }

        [[nodiscard]] FieldID getCurrentFieldID() const noexcept
        {
            if (!is_valid)
            {
                return 0;
            }
            return ts_tree_cursor_current_field_id(&impl);
        }

        [[nodiscard]] uint32_t getCurrentDescendantIndex() const noexcept
        {
            if (!is_valid)
            {
                return 0;
            }
            return ts_tree_cursor_current_descendant_index(&impl);
        }

        [[nodiscard]] uint32_t getDepthFromOrigin() const noexcept
        {
            if (!is_valid)
            {
                return 0;
            }
            return ts_tree_cursor_current_depth(&impl);
        }

        ////////////////////////////////////////////////////////////////
        // Operators
        ////////////////////////////////////////////////////////////////

        [[nodiscard]] bool operator==(const TreeCursor &other) const noexcept
        {
            if (is_valid != other.is_valid)
            {
                return false;
            }

            if (!is_valid)
            {
                return true; // Both are invalid, so they are conceptually equal
            }

            return getCurrentNode() == other.getCurrentNode();
        }

#if !TS_HAS_CXX20
        [[nodiscard]] bool operator!=(const TreeCursor &other) const noexcept
        {
            return !(*this == other);
        }
#endif

        TreeCursor &operator=(const TreeCursor &other) = delete;

        TreeCursor &operator=(TreeCursor &&other) noexcept
        {
            std::swap(impl, other.impl);
            std::swap(is_valid, other.is_valid);
            return *this;
        }

    private:
        TSTreeCursor impl;
        bool         is_valid = false;
    };

    // To avoid cyclic dependencies and ODR violations, we define all methods
    // *using* TreeCursors inline after the definition of TreeCursor itself.
    [[nodiscard]] inline TreeCursor Node::getCursor() const noexcept
    {
        return TreeCursor(impl);
    }

    /////////////////////////////////////////////////////////////////////////////
    // QueryCapture
    // A capture of a specific node by a specific name within a query pattern.
    /////////////////////////////////////////////////////////////////////////////

    struct QueryCapture
    {
        Node     node;
        uint32_t index;

        QueryCapture() noexcept : node(Node::null()), index(0)
        {}

        // For easy conversion from C API
        QueryCapture(const TSQueryCapture &capture) noexcept : node(capture.node), index(capture.index)
        {}
    };

    /////////////////////////////////////////////////////////////////////////////
    // QueryMatch
    // A single match found by a query, containing one or more captures.
    /////////////////////////////////////////////////////////////////////////////

    struct QueryMatch
    {
        uint32_t id;
        uint16_t pattern_index;

        // A vector is used for easy access; however, note that data copying occurs only when the QueryMatch object is
        // instantiated.
        std::vector<QueryCapture> captures;

        QueryMatch() noexcept : id(0), pattern_index(0)
        {}

        explicit QueryMatch(const TSQueryMatch &match) : id(match.id), pattern_index(match.pattern_index)
        {
            captures.assign(match.captures, match.captures + match.capture_count);
        }
    };

    /////////////////////////////////////////////////////////////////////////////
    // QueryCursorState
    // The current state of an active query cursor.
    /////////////////////////////////////////////////////////////////////////////

    struct QueryCursorState
    {
        uint32_t current_byte_offset;

        explicit QueryCursorState(const TSQueryCursorState *state) noexcept
            : current_byte_offset(state->current_byte_offset)
        {}

        explicit QueryCursorState(const TSQueryCursorState &state) noexcept
            : current_byte_offset(state.current_byte_offset)
        {}
    };

    /////////////////////////////////////////////////////////////////////////////
    // QueryCursorOptions
    // Configuration options for a query cursor, such as progress callbacks.
    /////////////////////////////////////////////////////////////////////////////

    using QueryCursorOptions = details::OptionsBase<TSQueryCursorOptions, TSQueryCursorState, QueryCursorState>;

    /////////////////////////////////////////////////////////////////////////////
    // QueryPredicateStep
    // A single step in a predicate defined within a query pattern.
    /////////////////////////////////////////////////////////////////////////////

    struct QueryPredicateStep
    {
        uint32_t               value_id;
        QueryPredicateStepType type;

        QueryPredicateStep(const TSQueryPredicateStep *predicate_step) noexcept
            : type(static_cast<QueryPredicateStepType>(predicate_step->type)), value_id(predicate_step->value_id)
        {}

        QueryPredicateStep(const TSQueryPredicateStep &predicate_step) noexcept
            : type(static_cast<QueryPredicateStepType>(predicate_step.type)), value_id(predicate_step.value_id)
        {}
    };

    /////////////////////////////////////////////////////////////////////////////
    // Query
    // Compiled set of patterns used to search for structures in a syntax tree.
    /////////////////////////////////////////////////////////////////////////////

    class Query
    {
    public:
        ////////////////////////////////////////////////////////////////
        // Lifecycle
        ////////////////////////////////////////////////////////////////

        Query(Language language, details::StringViewParameter source)
        {
            if (!language.isValid())
            {
                throw std::runtime_error("Tree-sitter: Passed Language for Query was invalid");
            }

            if (source.size() > std::numeric_limits<uint32_t>::max())
            {
                throw std::length_error("Tree-sitter: Input source exceeds maximum size of 4GB");
            }

            uint32_t     error_offset;
            TSQueryError error_type;

            TSQuery *query = ts_query_new(language,
                                          source.data(),
                                          static_cast<uint32_t>(source.size()),
                                          &error_offset,
                                          &error_type);

            details::QueryErrorHelper::validate(error_offset, error_type);

            if (!query)
            {
                throw std::runtime_error("Tree-sitter: Failed to create Query");
            }

            impl.reset(query);
        }

#if TS_HAS_CXX23
        [[nodiscard]] static inline std::expected<Query, std::string> create(
                Language                     language,
                details::StringViewParameter source) noexcept
        {
            if (!language.isValid())
            {
                return std::unexpected("Tree-sitter: Passed Language for Query was invalid");
            }

            if (source.size() > std::numeric_limits<uint32_t>::max())
            {
                return std::unexpected("Tree-sitter: Input source exceeds maximum size of 4GB");
            }

            uint32_t     error_offset;
            TSQueryError error_type;

            TSQuery *query = ts_query_new(language,
                                          source.data(),
                                          static_cast<uint32_t>(source.size()),
                                          &error_offset,
                                          &error_type);

            if (error_type != TSQueryErrorNone)
            {
                if (query)
                {
                    ts_query_delete(query);
                }
                return std::unexpected(details::QueryErrorHelper::format_error(error_offset, error_type));
            }

            if (!query)
            {
                return std::unexpected("Tree-sitter: Failed to create Query");
            }

            return Query(query);
        }
#endif

        ////////////////////////////////////////////////////////////////
        // Counts
        ////////////////////////////////////////////////////////////////

        [[nodiscard]] uint32_t getPatternCount() const noexcept
        {
            return ts_query_pattern_count(impl.get());
        }

        [[nodiscard]] uint32_t getCaptureCount() const noexcept
        {
            return ts_query_capture_count(impl.get());
        }

        [[nodiscard]] uint32_t getStringCount() const noexcept
        {
            return ts_query_string_count(impl.get());
        }

        ////////////////////////////////////////////////////////////////
        // Pattern Information
        ////////////////////////////////////////////////////////////////

        [[nodiscard]] bool isPatternRooted(uint32_t pattern_index) const noexcept
        {
            return ts_query_is_pattern_rooted(impl.get(), pattern_index);
        }

        [[nodiscard]] bool isPatternNonLocal(uint32_t pattern_index) const noexcept
        {
            return ts_query_is_pattern_non_local(impl.get(), pattern_index);
        }

        [[nodiscard]] bool isPatternGuaranteedAtStep(uint32_t byte_offset) const noexcept
        {
            return ts_query_is_pattern_guaranteed_at_step(impl.get(), byte_offset);
        }

        [[nodiscard]] Extent<uint32_t> getByteRangeForPattern(uint32_t pattern_index) const noexcept
        {
            return { ts_query_start_byte_for_pattern(impl.get(), pattern_index),
                     ts_query_end_byte_for_pattern(impl.get(), pattern_index) };
        }

        ////////////////////////////////////////////////////////////////
        // Predicates
        ////////////////////////////////////////////////////////////////

        [[nodiscard]] std::vector<QueryPredicateStep> getAllPredicatesForPattern(uint32_t pattern_index) const
        {
            uint32_t                    count = 0;
            const TSQueryPredicateStep *array = ts_query_predicates_for_pattern(impl.get(), pattern_index, &count);

            if (!array || count == 0)
            {
                return {};
            }

            std::vector<QueryPredicateStep> vec;
            vec.reserve(count);
            for (uint32_t i = 0; i < count; ++i)
            {
                vec.emplace_back(array[i]);
            }

            return vec;
        }

        ////////////////////////////////////////////////////////////////
        // Capture & String Resolution
        ////////////////////////////////////////////////////////////////

        [[nodiscard]] details::StringViewReturn getCaptureNameForID(uint32_t id) const noexcept
        {
            uint32_t    length;
            const char *name = ts_query_capture_name_for_id(impl.get(), id, &length);
            return details::make_view(name, static_cast<size_t>(length));
        }

        [[nodiscard]] Quantifier getCaptureQuantifierForID(uint32_t pattern_index, uint32_t capture_index) const
        {
            return static_cast<Quantifier>(
                    ts_query_capture_quantifier_for_id(impl.get(), pattern_index, capture_index));
        }

        [[nodiscard]] details::StringViewReturn getStringValueForID(uint32_t id) const noexcept
        {
            uint32_t    length;
            const char *name = ts_query_string_value_for_id(impl.get(), id, &length);
            return details::make_view(name, static_cast<size_t>(length));
        }

        ////////////////////////////////////////////////////////////////
        // Modification
        ////////////////////////////////////////////////////////////////

        void disableCapture(details::StringViewParameter name) const
        {
            if (name.size() > std::numeric_limits<uint32_t>::max())
            {
                throw std::length_error("Tree-sitter: Input name exceeds maximum size of 4GB");
            }

            ts_query_disable_capture(impl.get(), name.data(), static_cast<uint32_t>(name.size()));
        }

        void disablePattern(uint32_t pattern_index) const noexcept
        {
            ts_query_disable_pattern(impl.get(), pattern_index);
        }

        ////////////////////////////////////////////////////////////////
        // Operators
        ////////////////////////////////////////////////////////////////

        [[nodiscard]] operator const TSQuery *() const noexcept
        {
            return impl.get();
        }

    private:
        std::unique_ptr<TSQuery, decltype(&ts_query_delete)> impl{ nullptr, ts_query_delete };

#if TS_HAS_CXX23
        explicit Query(TSQuery *raw_query) : impl(raw_query, ts_query_delete)
        {}
#endif
    };

    /////////////////////////////////////////////////////////////////////////////
    // QueryCursor
    // Executes queries and iterates over matches.
    /////////////////////////////////////////////////////////////////////////////

    class QueryCursor
    {
    public:
        ////////////////////////////////////////////////////////////////
        // Lifecycle
        ////////////////////////////////////////////////////////////////

        QueryCursor() noexcept : impl(ts_query_cursor_new())
        {}

        // In C API QueryCursor don't have copy function
        QueryCursor(const QueryCursor &)     = delete;
        QueryCursor(QueryCursor &&) noexcept = default;

        ////////////////////////////////////////////////////////////////
        // Limits
        ////////////////////////////////////////////////////////////////

        [[nodiscard]] uint32_t getMatchLimit() noexcept
        {
            return ts_query_cursor_match_limit(impl.get());
        }

        void setMatchLimit(uint32_t limit) noexcept
        {
            ts_query_cursor_set_match_limit(impl.get(), limit);
        }

        [[nodiscard]] bool didExceedMatchLimit() noexcept
        {
            return ts_query_cursor_did_exceed_match_limit(impl.get());
        }

        void setMaxStartDepth(uint32_t max_start_depth) noexcept
        {
            ts_query_cursor_set_max_start_depth(impl.get(), max_start_depth);
        }

        void resetMaxStartDepth() noexcept
        {
            ts_query_cursor_set_max_start_depth(impl.get(), std::numeric_limits<uint32_t>::max());
        }

        ////////////////////////////////////////////////////////////////
        // Ranges
        ////////////////////////////////////////////////////////////////

        [[nodiscard]] bool setByteRange(Extent<uint32_t> range) noexcept
        {
            return ts_query_cursor_set_byte_range(impl.get(), range.start, range.end);
        }

        [[nodiscard]] bool setPointRange(Extent<Point> range) noexcept
        {
            return ts_query_cursor_set_point_range(impl.get(), range.start, range.end);
        }

        [[nodiscard]] bool setContainingByteRange(Extent<uint32_t> range) noexcept
        {
            return ts_query_cursor_set_containing_byte_range(impl.get(), range.start, range.end);
        }

        [[nodiscard]] bool setContainingPointRange(Extent<Point> range) noexcept
        {
            return ts_query_cursor_set_containing_point_range(impl.get(), range.start, range.end);
        }

        ////////////////////////////////////////////////////////////////
        // Execution
        ////////////////////////////////////////////////////////////////

        void exec(const Query &query, Node node, details::OptionalParam<const QueryCursorOptions> query_options = {})
        {
            const TSQueryCursorOptions *
                    raw_options = details::get_raw<const QueryCursorOptions, const TSQueryCursorOptions>(query_options);

            if (raw_options)
            {
                ts_query_cursor_exec_with_options(impl.get(), query, node.impl, raw_options);
            }
            else
            {
                ts_query_cursor_exec(impl.get(), query, node.impl);
            }
        }

        ////////////////////////////////////////////////////////////////
        // Iteration
        ////////////////////////////////////////////////////////////////

        [[nodiscard]] bool nextMatch(QueryMatch &match) noexcept
        {
            TSQueryMatch c_match{};
            if (ts_query_cursor_next_match(impl.get(), &c_match))
            {
                match = QueryMatch(c_match);
                return true;
            }
            return false;
        }

        [[nodiscard]] bool nextCapture(QueryMatch &match, uint32_t &capture_index) noexcept
        {
            TSQueryMatch c_match{};
            uint32_t     c_capture_index = 0;

            if (ts_query_cursor_next_capture(impl.get(), &c_match, &c_capture_index))
            {
                match         = QueryMatch(c_match);
                capture_index = c_capture_index;
                return true;
            }
            return false;
        }

        void removeMatch(uint32_t match_id) noexcept
        {
            return ts_query_cursor_remove_match(impl.get(), match_id);
        }

        ////////////////////////////////////////////////////////////////
        // Operators
        ////////////////////////////////////////////////////////////////

        // In C API QueryCursor don't have copy function
        QueryCursor &operator=(const QueryCursor &)     = delete;
        QueryCursor &operator=(QueryCursor &&) noexcept = default;

    private:
        std::unique_ptr<TSQueryCursor, details::FreeHelper> impl;
    };

    /////////////////////////////////////////////////////////////////////////////
    // LookaheadIterator
    // Iterates over possible symbols in a given parse state.
    /////////////////////////////////////////////////////////////////////////////

    class LookaheadIterator
    {
    public:
        ////////////////////////////////////////////////////////////////
        // Lifecycle
        ////////////////////////////////////////////////////////////////

        LookaheadIterator(Language language, StateID state)
        {
            if (!language.isValid())
            {
                throw std::runtime_error("Tree-sitter: Passed Language for lookahead iterator is not valid");
            }

            impl.reset(ts_lookahead_iterator_new(language, state));
            if (!impl)
            {
                throw std::runtime_error("Tree-sitter: Invalid parse state for lookahead iterator");
            }
        }

        // Copying is not supported by the underlying C API.
        LookaheadIterator(const LookaheadIterator &)          = delete;
        LookaheadIterator(LookaheadIterator &&other) noexcept = default;

        ////////////////////////////////////////////////////////////////
        // State Control
        ////////////////////////////////////////////////////////////////

        [[nodiscard]] bool resetState(StateID state) noexcept
        {
            return ts_lookahead_iterator_reset_state(impl.get(), state);
        }

        [[nodiscard]] bool reset(Language language, StateID state) noexcept
        {
            return ts_lookahead_iterator_reset(impl.get(), language, state);
        }

        ////////////////////////////////////////////////////////////////
        // Property Accessors
        ////////////////////////////////////////////////////////////////

        [[nodiscard]] Language getLanguage() const noexcept
        {
            return Language{ ts_lookahead_iterator_language(impl.get()) };
        }

        [[nodiscard]] Symbol getCurrentSymbol() const noexcept
        {
            return ts_lookahead_iterator_current_symbol(impl.get());
        }

        [[nodiscard]] details::StringViewReturn getCurrentSymbolName() const
        {
            return details::make_view(ts_lookahead_iterator_current_symbol_name(impl.get()));
        }

        ////////////////////////////////////////////////////////////////
        // Navigation
        ////////////////////////////////////////////////////////////////

        [[nodiscard]] bool next() noexcept
        {
            return ts_lookahead_iterator_next(impl.get());
        }

        ////////////////////////////////////////////////////////////////
        // Operators
        ////////////////////////////////////////////////////////////////

        LookaheadIterator &operator=(const LookaheadIterator &)          = delete;
        LookaheadIterator &operator=(LookaheadIterator &&other) noexcept = default;

        [[nodiscard]] operator const TSLookaheadIterator *() const noexcept
        {
            return impl.get();
        }

    private:
        std::unique_ptr<TSLookaheadIterator, details::FreeHelper> impl = nullptr;
    };

    [[nodiscard]] inline LookaheadIterator Language::getLookaheadIterator(StateID state) const
    {
        if (!is_valid)
        {
            throw std::logic_error("Tree-sitter: Can't create LookaheadIterator for invalid Language");
        }

        return LookaheadIterator(impl.get(), state);
    }

#if defined(CPP_TREE_SITTER_FEATURE_WASM)
    /////////////////////////////////////////////////////////////////////////////
    // WasmStore
    // Manages WebAssembly environments and loading Wasm-based languages.
    /////////////////////////////////////////////////////////////////////////////

    class WasmStore
    {
    public:
        //////////////////////////////////////////////////////////////
        // Lifecycle
        //////////////////////////////////////////////////////////////

        WasmStore(WasmEngine *engine)
        {
            TSWasmError  error{ TSWasmErrorKindNone, nullptr };
            TSWasmStore *store = ts_wasm_store_new(engine, &error);

            details::WasmErrorHelper::validate(error);

            if (!store)
            {
                throw std::runtime_error("Tree-sitter: Failed to create Wasm store");
            }
            impl.reset(store);
            is_valid = impl.get() != nullptr;
        }

        explicit WasmStore(TSWasmStore *store) noexcept : impl{ store, ts_wasm_store_delete }
        {
            is_valid = store != nullptr;
        }

        WasmStore(const WasmStore &)     = delete;
        WasmStore(WasmStore &&) noexcept = default;

#if TS_HAS_CXX23
        [[nodiscard]] static std::expected<WasmStore, std::string> create(WasmEngine *engine) noexcept
        {
            TSWasmError  error{ TSWasmErrorKindNone, nullptr };
            TSWasmStore *store = ts_wasm_store_new(engine, &error);

            if (error.kind != TSWasmErrorKindNone)
            {
                std::string err_msg = details::WasmErrorHelper::format_error(error);
                if (error.message)
                {
                    std::free(error.message);
                }
                return std::unexpected(err_msg);
            }

            if (!store)
            {
                return std::unexpected("Tree-sitter: Failed to create Wasm store");
            }

            return WasmStore(store);
        }
#endif

        //////////////////////////////////////////////////////////////
        // Methods
        //////////////////////////////////////////////////////////////

        [[nodiscard]] Language loadLanguage(details::StringViewParameter name, details::StringViewParameter wasm_buffer)
        {
            if (!is_valid)
            {
                throw std::length_error("Tree-sitter: Wasm Store is invalid");
            }

            if (wasm_buffer.size() > std::numeric_limits<uint32_t>::max())
            {
                throw std::length_error("Tree-sitter: Input wasm_buffer exceeds maximum size of 4GB");
            }

            TSWasmError       error{ TSWasmErrorKindNone, nullptr };
            const TSLanguage *lang = ts_wasm_store_load_language(impl.get(),
                                                                 name.data(),
                                                                 wasm_buffer.data(),
                                                                 static_cast<uint32_t>(wasm_buffer.size()),
                                                                 &error);

            details::WasmErrorHelper::validate(error);
            return Language{ lang };
        }

        [[nodiscard]] Language loadLanguage(details::StringViewParameter file_path)
        {
            if (!is_valid)
            {
                throw std::length_error("Tree-sitter: Wasm Store is invalid");
            }

            if (file_path.size() > std::numeric_limits<uint32_t>::max())
            {
                throw std::length_error("Tree-sitter: Input file_path exceeds maximum size of 4GB");
            }

            const std::string path(file_path);
            std::ifstream     file(path, std::ios::binary | std::ios::ate);
            if (!file.is_open())
            {
                throw std::runtime_error("Tree-sitter: Failed to open wasm file: " + path);
            }

            file.seekg(0, std::ios::end);
            std::streamsize size = file.tellg();
            if (size < 0)
            {
                throw std::runtime_error("Tree-sitter: Failed to determine file size: " + path);
            }
            file.seekg(0, std::ios::beg);

            std::string buffer;
            buffer.reserve(static_cast<size_t>(size));
            buffer.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

            std::string lang_name = details::get_grammar_name_from_file(path);

            return loadLanguage(lang_name, buffer);
        }

#if TS_HAS_CXX23
        [[nodiscard]] std::expected<Language, std::string> loadLanguageExpected(
                details::StringViewParameter name,
                details::StringViewParameter wasm_buffer) noexcept
        {
            if (!is_valid)
            {
                return std::unexpected("Tree-sitter: Wasm Store is invalid");
            }

            if (wasm_buffer.size() > std::numeric_limits<uint32_t>::max())
            {
                return std::unexpected("Tree-sitter: Input wasm_buffer exceeds maximum size of 4GB");
            }

            TSWasmError       error{ TSWasmErrorKindNone, nullptr };
            const TSLanguage *lang = ts_wasm_store_load_language(impl.get(),
                                                                 name.data(),
                                                                 wasm_buffer.data(),
                                                                 static_cast<uint32_t>(wasm_buffer.size()),
                                                                 &error);

            if (error.kind != TSWasmErrorKindNone)
            {
                std::string err_msg = details::WasmErrorHelper::format_error(error);
                if (error.message)
                {
                    std::free(error.message);
                }
                return std::unexpected(err_msg);
            }

            return Language{ lang };
        }

        [[nodiscard]] std::expected<Language, std::string> loadLanguageExpected(details::StringViewParameter file_path)
        {
            if (!is_valid)
            {
                return std::unexpected("Tree-sitter: Wasm Store is invalid");
            }

            if (file_path.size() > std::numeric_limits<uint32_t>::max())
            {
                return std::unexpected("Tree-sitter: Input file_path exceeds maximum size of 4GB");
            }

            const std::string path(file_path);
            std::ifstream     file(path, std::ios::binary | std::ios::ate);
            if (!file.is_open())
            {
                return std::unexpected("Tree-sitter: Failed to open wasm file: " + path);
            }

            file.seekg(0, std::ios::end);
            std::streamsize size = file.tellg();
            if (size < 0)
            {
                return std::unexpected("Tree-sitter: Failed to determine file size: " + path);
            }
            file.seekg(0, std::ios::beg);

            std::string buffer;
            buffer.reserve(static_cast<size_t>(size));
            buffer.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

            std::string lang_name = details::get_grammar_name_from_file(path);

            return loadLanguageExpected(lang_name, buffer);
        }
#endif

        [[nodiscard]] size_t getLanguageCount() const
        {
            if (!is_valid)
            {
                throw std::length_error("Tree-sitter: Wasm Store is invalid");
            }

            return ts_wasm_store_language_count(impl.get());
        }

        [[nodiscard]] bool isValid() const noexcept
        {
            return is_valid;
        }

        //////////////////////////////////////////////////////////////
        // Operators
        //////////////////////////////////////////////////////////////

        WasmStore &operator=(const WasmStore &)     = delete;
        WasmStore &operator=(WasmStore &&) noexcept = default;

        [[nodiscard]] operator TSWasmStore *() const noexcept
        {
            return impl.get();
        }

    private:
        std::unique_ptr<TSWasmStore, decltype(&ts_wasm_store_delete)> impl{ nullptr, ts_wasm_store_delete };
        bool                                                          is_valid = false;

        void setInvalid()
        {
            // We need to set impl as nullptr but don't call deleter on stored pointer
            impl.release();
            is_valid = false;
        }

        friend class Parser;
    };

    [[nodiscard]] inline void Parser::setWasmStore(WasmStore &store) noexcept
    {
        ts_parser_set_wasm_store(impl.get(), store);
        wasm_store_set = true;
        store.setInvalid();
    }

    [[nodiscard]] inline WasmStore Parser::takeWasmStore() noexcept
    {
        Language current = getCurrentLanguage();
        if (current.isValid() && current.isWasm())
        {
            language_set = false;
        }
        TSWasmStore *raw_store = ts_parser_take_wasm_store(impl.get());
        wasm_store_set         = false;
        return WasmStore{ raw_store };
    }

#if TS_HAS_CXX23
    [[nodiscard]] inline std::expected<Parser, std::string> Parser::create(Language language, WasmStore &store) noexcept
    {
        if (!language.isValid())
        {
            return std::unexpected("Tree-sitter: Passed Language for Parser was invalid");
        }

        Parser parser;
        parser.setWasmStore(store);
        if (auto res = parser.setLanguage(language); !res.has_value())
        {
            store = parser.takeWasmStore();
            return std::unexpected(res.error());
        }

        return parser;
    }
#endif
#endif

    /////////////////////////////////////////////////////////////////////////////
    // Child Node Iterators
    // STL-compatible iterators for processing Node children.
    /////////////////////////////////////////////////////////////////////////////

    class ChildIteratorSentinel
    {};

    class ChildIterator
    {
    public:
        ////////////////////////////////////////////////////////////////
        // Aliases
        ////////////////////////////////////////////////////////////////

        using value_type        = ts::Node;
        using difference_type   = std::ptrdiff_t;
        using iterator_category = std::input_iterator_tag;

        class Child
        {
        public:
            explicit Child(value_type v) noexcept : value(v)
            {}

#if TS_HAS_CXX23
            template <typename Self>
            auto *operator->(this Self &&self) noexcept
            {
                return &self.value;
            }
#else
            const value_type *operator->() const noexcept
            {
                return &value;
            }
            value_type *operator->() noexcept
            {
                return &value;
            }
#endif

        private:
            value_type value;
        };

        ////////////////////////////////////////////////////////////////
        // Lifecycle
        ////////////////////////////////////////////////////////////////

        ChildIterator() : atEnd(true)
        {}

        explicit ChildIterator(const ts::Node &node) noexcept
            : cursor{ node.getCursor() }, atEnd{ node.isNull() || !cursor.gotoFirstChild() }
        {}

        ChildIterator(const ChildIterator &)     = delete;
        ChildIterator(ChildIterator &&) noexcept = default;

        ////////////////////////////////////////////////////////////////
        // Get
        ////////////////////////////////////////////////////////////////

        [[nodiscard]] value_type get() const noexcept
        {
            return cursor.getCurrentNode();
        }

        [[nodiscard]] value_type operator*() const noexcept
        {
            return get();
        }

        ////////////////////////////////////////////////////////////////
        // Advance
        ////////////////////////////////////////////////////////////////

        Child operator->() const noexcept
        {
            // Dereference this iterator to get the value,
            // then wrap it in the safe proxy.
            return Child(get());
        }

        ChildIterator &operator++() noexcept
        {
            atEnd = !cursor.gotoNextSibling();
            return *this;
        }

        ChildIterator &operator++(int) noexcept
        {
            return ++(*this);
        }

        ////////////////////////////////////////////////////////////////
        // Comparision
        ////////////////////////////////////////////////////////////////

        [[nodiscard]] friend bool operator==(const ChildIterator &a, const ChildIteratorSentinel &) noexcept
        {
            return a.atEnd;
        }

#if !TS_HAS_CXX20
        [[nodiscard]] friend bool operator!=(const ChildIterator &a, const ChildIteratorSentinel &b) noexcept
        {
            return !(a == b);
        }
#endif

        [[nodiscard]] friend bool operator==(const ChildIteratorSentinel &b, const ChildIterator &a) noexcept
        {
            return a == b;
        }

#if !TS_HAS_CXX20
        [[nodiscard]] friend bool operator!=(const ChildIteratorSentinel &b, const ChildIterator &a) noexcept
        {
            return a != b;
        }
#endif

        ////////////////////////////////////////////////////////////////
        // Operators
        ////////////////////////////////////////////////////////////////

        ChildIterator &operator=(const ChildIterator &)     = delete;
        ChildIterator &operator=(ChildIterator &&) noexcept = default;

    private:
        ts::TreeCursor cursor;
        bool           atEnd = false;
    };

#if TS_HAS_CXX20
    struct Children : public std::ranges::view_interface<Children>
#else
    struct Children
#endif
    {
        using iterator = ChildIterator;
        using sentinel = ChildIteratorSentinel;

        explicit Children(const ts::Node &n) noexcept : node_ref(n)
        {}

        iterator begin() const noexcept
        {
            return ChildIterator{ node_ref };
        }

        sentinel end() const noexcept
        {
            return {};
        }

        [[nodiscard]] size_t size() const noexcept
        {
            return static_cast<size_t>(node_ref.getChildCount());
        }

        ts::Node node_ref;
    };

#if TS_HAS_CXX20
    static_assert(std::input_iterator<ChildIterator>);
    static_assert(std::sentinel_for<ChildIteratorSentinel, ChildIterator>);
#endif

    /////////////////////////////////////////////////////////////////////////////
    // Visitor
    // High-level utility for depth-first traversal of a syntax tree.
    /////////////////////////////////////////////////////////////////////////////

    ////////////////////////////////////////////////////////////////
    // Concepts
    ////////////////////////////////////////////////////////////////

#if TS_HAS_CXX20
    template <typename T>
    concept VisitorConcept = requires {
        { std::declval<T>()(std::declval<Node>()) } -> std::convertible_to<bool>;
    };
#elif TS_HAS_CXX17
    template <typename T, typename = void>
    struct VisitorConcept : std::false_type
    {};

    template <typename T>
    struct VisitorConcept<T,
                          std::void_t<std::enable_if_t<
                                  std::is_convertible_v<decltype(std::declval<T>()(std::declval<Node>())), bool>>>>
        : std::true_type
    {};
#endif

    ////////////////////////////////////////////////////////////////
    // Function
    ////////////////////////////////////////////////////////////////

#if TS_HAS_CXX20
    template <VisitorConcept F>
#elif TS_HAS_CXX17
    template <typename F, std::enable_if_t<VisitorConcept<F>::value, bool> = true>
#else
template <typename F>
#endif
    // Callback should return `true` if wants to break from loop.
    void visit(Node root, F &&callback)
    {
        if (root.isNull())
        {
            return;
        }

        TreeCursor     cursor      = root.getCursor();
        const uint32_t start_depth = cursor.getDepthFromOrigin();

        while (true)
        {
            if (callback(cursor.getCurrentNode()))
            {
                break;
            }

            // 1. Go to first child (down)
            if (cursor.gotoFirstChild())
            {
                continue;
            }

            // 2. If there is no child go to neighbour (sideway)
            if (cursor.gotoNextSibling())
            {
                continue;
            }

            // 3. If no child and neighbours go up if you find next neighbours but not before `root`
            bool found_next = false;
            while (cursor.getDepthFromOrigin() > start_depth)
            {
                if (cursor.gotoParent())
                {
                    if (cursor.gotoNextSibling())
                    {
                        found_next = true;
                        break;
                    }
                }
                else
                {
                    break;
                }
            }

            if (!found_next)
            {
                break; // Everything was checked
            }
        }
    }

} // namespace ts
#endif // CPP_TREE_SITTER_HPP
