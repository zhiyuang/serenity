/*
 * Copyright (c) 2022, Daniel Ehrenberg <dan@littledan.dev>
 * Copyright (c) 2022, Andrew Kaster <akaster@serenityos.org>
 * Copyright (c) 2023, Kenneth Myhra <kennethmyhra@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/StdLibExtras.h>
#include <AK/String.h>
#include <AK/Vector.h>
#include <LibJS/Forward.h>
#include <LibJS/Runtime/BigInt.h>
#include <LibJS/Runtime/BooleanObject.h>
#include <LibJS/Runtime/Date.h>
#include <LibJS/Runtime/NumberObject.h>
#include <LibJS/Runtime/PrimitiveString.h>
#include <LibJS/Runtime/StringObject.h>
#include <LibJS/Runtime/VM.h>
#include <LibWeb/Bindings/ExceptionOrUtils.h>
#include <LibWeb/HTML/StructuredSerialize.h>
#include <LibWeb/WebIDL/ExceptionOr.h>

namespace Web::HTML {

// Binary format:
// A list of adjacent shallow values, which may contain references to other
// values (noted by their position in the list, one value following another).
// This list represents the "memory" in the StructuredSerialize algorithm.
// The first item in the list is the root, i.e., the value of everything.
// The format is generally u32-aligned (hence this leaking out into the type)
// Each value has a length based on its type, as defined below.
//
// (Should more redundancy be added, e.g., for lengths/positions of values?)

enum ValueTag {
    // Unused, for ease of catching bugs.
    Empty,

    // UndefinedPrimitive is serialized indicating that the Type is Undefined, no value is serialized.
    UndefinedPrimitive,

    // NullPrimitive is serialized indicating that the Type is Null, no value is serialized.
    NullPrimitive,

    // Following u32 is the boolean value.
    BooleanPrimitive,

    // Following two u32s are the double value.
    NumberPrimitive,

    // The BigIntPrimitive is serialized as a string in base 10 representation.
    // Following two u32s representing the length of the string, then the following u32s, equal to size, is the string representation.
    BigIntPrimitive,

    // Following two u32s representing the length of the string, then the following u32s, equal to size, is the string representation.
    StringPrimitive,

    BooleanObject,

    NumberObject,

    StringObject,

    DateObject,

    // TODO: Define many more types

    // This tag or higher are understood to be errors
    ValueTagMax,
};

// Serializing and deserializing are each two passes:
// 1. Fill up the memory with all the values, but without translating references
// 2. Translate all the references into the appropriate form

class Serializer {
public:
    Serializer(JS::VM& vm)
        : m_vm(vm)
    {
    }

    WebIDL::ExceptionOr<void> serialize(JS::Value value)
    {
        if (value.is_undefined()) {
            m_serialized.append(ValueTag::UndefinedPrimitive);
        } else if (value.is_null()) {
            m_serialized.append(ValueTag::NullPrimitive);
        } else if (value.is_boolean()) {
            m_serialized.append(ValueTag::BooleanPrimitive);
            m_serialized.append(static_cast<u32>(value.as_bool()));
        } else if (value.is_number()) {
            m_serialized.append(ValueTag::NumberPrimitive);
            double number = value.as_double();
            m_serialized.append(bit_cast<u32*>(&number), 2);
        } else if (value.is_bigint()) {
            m_serialized.append(ValueTag::BigIntPrimitive);
            auto& val = value.as_bigint();
            TRY(serialize_string(m_serialized, TRY_OR_THROW_OOM(m_vm, val.to_string())));
        } else if (value.is_string()) {
            m_serialized.append(ValueTag::StringPrimitive);
            TRY(serialize_string(m_serialized, value.as_string()));
        } else if (value.is_object() && is<JS::BooleanObject>(value.as_object())) {
            m_serialized.append(ValueTag::BooleanObject);
            auto& boolean_object = static_cast<JS::BooleanObject&>(value.as_object());
            m_serialized.append(bit_cast<u32>(static_cast<u32>(boolean_object.boolean())));
        } else if (value.is_object() && is<JS::NumberObject>(value.as_object())) {
            m_serialized.append(ValueTag::NumberObject);
            auto& number_object = static_cast<JS::NumberObject&>(value.as_object());
            double const number = number_object.number();
            m_serialized.append(bit_cast<u32*>(&number), 2);
        } else if (value.is_object() && is<JS::StringObject>(value.as_object())) {
            m_serialized.append(ValueTag::StringObject);
            auto& string_object = static_cast<JS::StringObject&>(value.as_object());
            TRY(serialize_string(m_serialized, string_object.primitive_string()));
        } else if (value.is_object() && is<JS::Date>(value.as_object())) {
            m_serialized.append(ValueTag::DateObject);
            auto& date_object = static_cast<JS::Date&>(value.as_object());
            double const date_value = date_object.date_value();
            m_serialized.append(bit_cast<u32*>(&date_value), 2);
        } else {
            // TODO: Define many more types
            m_error = "Unsupported type"sv;
        }

        // Second pass: Update the objects to point to other objects in memory

        return {};
    }

    WebIDL::ExceptionOr<Vector<u32>> result()
    {
        if (m_error.is_null())
            return m_serialized;
        return throw_completion(WebIDL::DataCloneError::create(*m_vm.current_realm(), m_error));
    }

private:
    AK::StringView m_error;
    SerializationMemory m_memory; // JS value -> index
    SerializationRecord m_serialized;
    JS::VM& m_vm;

    WebIDL::ExceptionOr<void> serialize_string(Vector<u32>& vector, String const& string)
    {
        u64 const size = string.code_points().byte_length();
        // Append size of the string to the serialized structure.
        TRY_OR_THROW_OOM(m_vm, vector.try_append(bit_cast<u32*>(&size), 2));
        // Append the bytes of the string to the serialized structure.
        u64 byte_position = 0;
        ReadonlyBytes const bytes = { string.code_points().bytes(), string.code_points().byte_length() };
        while (byte_position < size) {
            u32 combined_value = 0;
            for (u8 i = 0; i < 4; ++i) {
                u8 const byte = bytes[byte_position];
                combined_value |= byte << (i * 8);
                byte_position++;
                if (byte_position == size)
                    break;
            }
            TRY_OR_THROW_OOM(m_vm, vector.try_append(combined_value));
        }
        return {};
    }

    WebIDL::ExceptionOr<void> serialize_string(Vector<u32>& vector, JS::PrimitiveString const& primitive_string)
    {
        auto string = TRY(Bindings::throw_dom_exception_if_needed(m_vm, [&primitive_string]() {
            return primitive_string.utf8_string();
        }));
        TRY(serialize_string(vector, string));
        return {};
    }
};

class Deserializer {
public:
    Deserializer(JS::VM& vm, JS::Realm& target_realm, SerializationRecord const& v)
        : m_vm(vm)
        , m_vector(v)
        , m_memory(target_realm.heap())
    {
    }

    WebIDL::ExceptionOr<void> deserialize()
    {
        // First pass: fill up the memory with new values
        u32 position = 0;
        while (position < m_vector.size()) {
            switch (m_vector[position++]) {
            case ValueTag::UndefinedPrimitive: {
                m_memory.append(JS::js_undefined());
                break;
            }
            case ValueTag::NullPrimitive: {
                m_memory.append(JS::js_null());
                break;
            }
            case ValueTag::BooleanPrimitive: {
                m_memory.append(JS::Value(static_cast<bool>(m_vector[position++])));
                break;
            }
            case ValueTag::NumberPrimitive: {
                u32 bits[2];
                bits[0] = m_vector[position++];
                bits[1] = m_vector[position++];
                double value = *bit_cast<double*>(&bits);
                m_memory.append(JS::Value(value));
                break;
            }
            case ValueTag::BigIntPrimitive: {
                auto big_int = TRY(deserialize_big_int_primitive(m_vm, m_vector, position));
                m_memory.append(JS::Value { big_int });
                break;
            }
            case ValueTag::StringPrimitive: {
                auto string = TRY(deserialize_string_primitive(m_vm, m_vector, position));
                m_memory.append(JS::Value { string });
                break;
            }
            case BooleanObject: {
                auto* realm = m_vm.current_realm();
                bool const value = static_cast<bool>(m_vector[position++]);
                m_memory.append(JS::BooleanObject::create(*realm, value));
                break;
            }
            case ValueTag::NumberObject: {
                auto* realm = m_vm.current_realm();
                u32 bits[2];
                bits[0] = m_vector[position++];
                bits[1] = m_vector[position++];
                double const value = *bit_cast<double*>(&bits);
                m_memory.append(JS::NumberObject::create(*realm, value));
                break;
            }
            case ValueTag::StringObject: {
                auto* realm = m_vm.current_realm();
                auto string = TRY(deserialize_string_primitive(m_vm, m_vector, position));
                m_memory.append(TRY(JS::StringObject::create(*realm, string, realm->intrinsics().string_prototype())));
                break;
            }
            case ValueTag::DateObject: {
                auto* realm = m_vm.current_realm();
                u32 bits[2];
                bits[0] = m_vector[position++];
                bits[1] = m_vector[position++];
                double const value = *bit_cast<double*>(&bits);
                m_memory.append(JS::Date::create(*realm, value));
                break;
            }
            default:
                m_error = "Unsupported type"sv;
                break;
            }
        }
        return {};
    }

    WebIDL::ExceptionOr<JS::Value> result()
    {
        if (m_error.is_null())
            return m_memory[0];
        return throw_completion(WebIDL::DataCloneError::create(*m_vm.current_realm(), m_error));
    }

private:
    JS::VM& m_vm;
    SerializationRecord const& m_vector;
    JS::MarkedVector<JS::Value> m_memory; // Index -> JS value
    StringView m_error;

    static WebIDL::ExceptionOr<JS::NonnullGCPtr<JS::PrimitiveString>> deserialize_string_primitive(JS::VM& vm, Vector<u32> const& vector, u32& position)
    {
        u32 size_bits[2];
        size_bits[0] = vector[position++];
        size_bits[1] = vector[position++];
        u64 const size = *bit_cast<u64*>(&size_bits);

        Vector<u8> bytes;
        TRY_OR_THROW_OOM(vm, bytes.try_ensure_capacity(size));
        u64 byte_position = 0;
        while (position < vector.size()) {
            for (u8 i = 0; i < 4; ++i) {
                bytes.append(vector[position] >> (i * 8) & 0xFF);
                byte_position++;
                if (byte_position == size)
                    break;
            }
            position++;
        }

        return TRY(Bindings::throw_dom_exception_if_needed(vm, [&vm, &bytes]() {
            return JS::PrimitiveString::create(vm, StringView { bytes });
        }));
    }

    static WebIDL::ExceptionOr<JS::NonnullGCPtr<JS::BigInt>> deserialize_big_int_primitive(JS::VM& vm, Vector<u32> const& vector, u32& position)
    {
        auto string = TRY(deserialize_string_primitive(vm, vector, position));
        auto string_view = TRY(Bindings::throw_dom_exception_if_needed(vm, [&string]() {
            return string->utf8_string_view();
        }));
        return JS::BigInt::create(vm, ::Crypto::SignedBigInteger::from_base(10, string_view.substring_view(0, string_view.length() - 1)));
    }
};

// https://html.spec.whatwg.org/multipage/structured-data.html#structuredserialize
WebIDL::ExceptionOr<SerializationRecord> structured_serialize(JS::VM& vm, JS::Value value)
{
    // 1. Return ? StructuredSerializeInternal(value, false).
    return structured_serialize_internal(vm, value, false, {});
}

// https://html.spec.whatwg.org/multipage/structured-data.html#structuredserializeforstorage
WebIDL::ExceptionOr<SerializationRecord> structured_serialize_for_storage(JS::VM& vm, JS::Value value)
{
    // 1. Return ? StructuredSerializeInternal(value, true).
    return structured_serialize_internal(vm, value, true, {});
}

// https://html.spec.whatwg.org/multipage/structured-data.html#structuredserializeinternal
WebIDL::ExceptionOr<SerializationRecord> structured_serialize_internal(JS::VM& vm, JS::Value value, bool for_storage, Optional<SerializationMemory> memory)
{
    // FIXME: Do the spec steps
    (void)for_storage;
    (void)memory;

    Serializer serializer(vm);
    TRY(serializer.serialize(value));
    return serializer.result(); // TODO: Avoid several copies of vector
}

// https://html.spec.whatwg.org/multipage/structured-data.html#structureddeserialize
WebIDL::ExceptionOr<JS::Value> structured_deserialize(JS::VM& vm, SerializationRecord const& serialized, JS::Realm& target_realm, Optional<SerializationMemory> memory)
{
    // FIXME: Do the spec steps
    (void)memory;

    Deserializer deserializer(vm, target_realm, serialized);
    TRY(deserializer.deserialize());
    return deserializer.result();
}

}
