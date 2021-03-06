// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "parser.h"

namespace fidl {

#define TOKEN_PRIMITIVE_TYPE_CASES                                                                 \
    case Token::Kind::Bool:                                                                        \
    case Token::Kind::Status:                                                                      \
    case Token::Kind::Int8:                                                                        \
    case Token::Kind::Int16:                                                                       \
    case Token::Kind::Int32:                                                                       \
    case Token::Kind::Int64:                                                                       \
    case Token::Kind::Uint8:                                                                       \
    case Token::Kind::Uint16:                                                                      \
    case Token::Kind::Uint32:                                                                      \
    case Token::Kind::Uint64:                                                                      \
    case Token::Kind::Float32:                                                                     \
    case Token::Kind::Float64

#define TOKEN_TYPE_CASES                                                                           \
    TOKEN_PRIMITIVE_TYPE_CASES:                                                                    \
    case Token::Kind::Identifier:                                                                  \
    case Token::Kind::Array:                                                                       \
    case Token::Kind::Vector:                                                                      \
    case Token::Kind::String:                                                                      \
    case Token::Kind::Handle:                                                                      \
    case Token::Kind::Request

#define TOKEN_LITERAL_CASES                                                                        \
    case Token::Kind::Default:                                                                     \
    case Token::Kind::True:                                                                        \
    case Token::Kind::False:                                                                       \
    case Token::Kind::NumericLiteral:                                                              \
    case Token::Kind::StringLiteral

namespace {
enum {
    More,
    Done,
};
} // namespace

decltype(nullptr) Parser::Fail() {
    if (ok_) {
        int line_number;
        auto surrounding_line = last_token_.location().SourceLine(&line_number);

        std::string error = "found unexpected token: ";
        error += last_token_.data();
        error += "\n";
        error += "on line #" + std::to_string(line_number) + ":\n\n";
        error += surrounding_line;
        error += "\n";

        error_reporter_->ReportError(error);
        ok_ = false;
    }
    return nullptr;
}

std::unique_ptr<Identifier> Parser::ParseIdentifier() {
    auto identifier = ConsumeToken(Token::Kind::Identifier);
    if (!Ok())
        return Fail();

    return std::make_unique<Identifier>(identifier);
}

std::unique_ptr<CompoundIdentifier> Parser::ParseCompoundIdentifier() {
    std::vector<std::unique_ptr<Identifier>> components;

    components.emplace_back(ParseIdentifier());
    if (!Ok())
        return Fail();

    auto parse_component = [&components, this]() {
        switch (Peek()) {
        default:
            return Done;

        case Token::Kind::Dot:
            ConsumeToken(Token::Kind::Dot);
            if (Ok())
                components.emplace_back(ParseIdentifier());
            return More;
        }
    };

    while (parse_component() == More) {
        if (!Ok())
            return Fail();
    }

    return std::make_unique<CompoundIdentifier>(std::move(components));
}

std::unique_ptr<StringLiteral> Parser::ParseStringLiteral() {
    auto string_literal = ConsumeToken(Token::Kind::StringLiteral);
    if (!Ok())
        return Fail();

    return std::make_unique<StringLiteral>(string_literal);
}

std::unique_ptr<NumericLiteral> Parser::ParseNumericLiteral() {
    auto numeric_literal = ConsumeToken(Token::Kind::NumericLiteral);
    if (!Ok())
        return Fail();

    return std::make_unique<NumericLiteral>(numeric_literal);
}

std::unique_ptr<TrueLiteral> Parser::ParseTrueLiteral() {
    ConsumeToken(Token::Kind::True);
    if (!Ok())
        return Fail();

    return std::make_unique<TrueLiteral>();
}

std::unique_ptr<FalseLiteral> Parser::ParseFalseLiteral() {
    ConsumeToken(Token::Kind::False);
    if (!Ok())
        return Fail();

    return std::make_unique<FalseLiteral>();
}

std::unique_ptr<DefaultLiteral> Parser::ParseDefaultLiteral() {
    ConsumeToken(Token::Kind::Default);
    if (!Ok())
        return Fail();

    return std::make_unique<DefaultLiteral>();
}

std::unique_ptr<Literal> Parser::ParseLiteral() {
    switch (Peek()) {
    case Token::Kind::StringLiteral:
        return ParseStringLiteral();

    case Token::Kind::NumericLiteral:
        return ParseNumericLiteral();

    case Token::Kind::True:
        return ParseTrueLiteral();

    case Token::Kind::False:
        return ParseFalseLiteral();

    case Token::Kind::Default:
        return ParseDefaultLiteral();

    default:
        return Fail();
    }
}

std::unique_ptr<Constant> Parser::ParseConstant() {
    switch (Peek()) {
    case Token::Kind::Identifier: {
        auto identifier = ParseCompoundIdentifier();
        if (!Ok())
            return Fail();
        return std::make_unique<IdentifierConstant>(std::move(identifier));
    }

    TOKEN_LITERAL_CASES : {
        auto literal = ParseLiteral();
        if (!Ok())
            return Fail();
        return std::make_unique<LiteralConstant>(std::move(literal));
    }

    default:
        return Fail();
    }
}

std::unique_ptr<Using> Parser::ParseUsing() {
    ConsumeToken(Token::Kind::Using);
    if (!Ok())
        return Fail();
    auto using_path = ParseCompoundIdentifier();
    if (!Ok())
        return Fail();

    std::unique_ptr<Identifier> maybe_alias;
    if (MaybeConsumeToken(Token::Kind::As)) {
        if (!Ok())
            return Fail();
        maybe_alias = ParseIdentifier();
        if (!Ok())
            return Fail();
    }

    return std::make_unique<Using>(std::move(using_path), std::move(maybe_alias));
}

std::unique_ptr<ArrayType> Parser::ParseArrayType() {
    ConsumeToken(Token::Kind::Array);
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::LeftAngle);
    if (!Ok())
        return Fail();
    auto element_type = ParseType();
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::RightAngle);
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::Colon);
    if (!Ok())
        return Fail();
    auto element_count = ParseConstant();
    if (!Ok())
        return Fail();

    return std::make_unique<ArrayType>(std::move(element_type), std::move(element_count));
}

std::unique_ptr<VectorType> Parser::ParseVectorType() {
    ConsumeToken(Token::Kind::Vector);
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::LeftAngle);
    if (!Ok())
        return Fail();
    auto element_type = ParseType();
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::RightAngle);
    if (!Ok())
        return Fail();

    std::unique_ptr<Constant> maybe_element_count;
    if (MaybeConsumeToken(Token::Kind::Colon)) {
        if (!Ok())
            return Fail();
        maybe_element_count = ParseConstant();
        if (!Ok())
            return Fail();
    }

    auto nullability = Nullability::Nonnullable;
    if (MaybeConsumeToken(Token::Kind::Question)) {
        nullability = Nullability::Nullable;
    }

    return std::make_unique<VectorType>(std::move(element_type), std::move(maybe_element_count),
                                        nullability);
}

std::unique_ptr<StringType> Parser::ParseStringType() {
    ConsumeToken(Token::Kind::String);
    if (!Ok())
        return Fail();

    std::unique_ptr<Constant> maybe_element_count;
    if (MaybeConsumeToken(Token::Kind::Colon)) {
        if (!Ok())
            return Fail();
        maybe_element_count = ParseConstant();
        if (!Ok())
            return Fail();
    }

    auto nullability = Nullability::Nonnullable;
    if (MaybeConsumeToken(Token::Kind::Question)) {
        nullability = Nullability::Nullable;
    }

    return std::make_unique<StringType>(std::move(maybe_element_count), nullability);
}

std::unique_ptr<HandleType> Parser::ParseHandleType() {
    ConsumeToken(Token::Kind::Handle);
    if (!Ok())
        return Fail();

    auto subtype = HandleType::Subtype::Handle;

    if (MaybeConsumeToken(Token::Kind::LeftAngle)) {
        if (!Ok())
            return Fail();
        switch (Peek()) {
        case Token::Kind::Process:
            subtype = HandleType::Subtype::Process;
            break;
        case Token::Kind::Thread:
            subtype = HandleType::Subtype::Thread;
            break;
        case Token::Kind::Vmo:
            subtype = HandleType::Subtype::Vmo;
            break;
        case Token::Kind::Channel:
            subtype = HandleType::Subtype::Channel;
            break;
        case Token::Kind::Event:
            subtype = HandleType::Subtype::Event;
            break;
        case Token::Kind::Port:
            subtype = HandleType::Subtype::Port;
            break;
        case Token::Kind::Interrupt:
            subtype = HandleType::Subtype::Interrupt;
            break;
        case Token::Kind::Iomap:
            subtype = HandleType::Subtype::Iomap;
            break;
        case Token::Kind::Pci:
            subtype = HandleType::Subtype::Pci;
            break;
        case Token::Kind::Log:
            subtype = HandleType::Subtype::Log;
            break;
        case Token::Kind::Socket:
            subtype = HandleType::Subtype::Socket;
            break;
        case Token::Kind::Resource:
            subtype = HandleType::Subtype::Resource;
            break;
        case Token::Kind::Eventpair:
            subtype = HandleType::Subtype::Eventpair;
            break;
        case Token::Kind::Job:
            subtype = HandleType::Subtype::Job;
            break;
        case Token::Kind::Vmar:
            subtype = HandleType::Subtype::Vmar;
            break;
        case Token::Kind::Fifo:
            subtype = HandleType::Subtype::Fifo;
            break;
        case Token::Kind::Hypervisor:
            subtype = HandleType::Subtype::Hypervisor;
            break;
        case Token::Kind::Guest:
            subtype = HandleType::Subtype::Guest;
            break;
        case Token::Kind::Timer:
            subtype = HandleType::Subtype::Timer;
            break;
        default:
            return Fail();
        }
        Consume();
        if (!Ok())
            return Fail();

        ConsumeToken(Token::Kind::RightAngle);
        if (!Ok())
            return Fail();
    }

    auto nullability = Nullability::Nonnullable;
    if (MaybeConsumeToken(Token::Kind::Question)) {
        nullability = Nullability::Nullable;
    }

    return std::make_unique<HandleType>(subtype, nullability);
}

std::unique_ptr<PrimitiveType> Parser::ParsePrimitiveType() {
    PrimitiveType::TypeKind type_kind;

    switch (Peek()) {
    case Token::Kind::Bool:
        type_kind = PrimitiveType::TypeKind::Bool;
        break;
    case Token::Kind::Status:
        type_kind = PrimitiveType::TypeKind::Status;
        break;
    case Token::Kind::Int8:
        type_kind = PrimitiveType::TypeKind::Int8;
        break;
    case Token::Kind::Int16:
        type_kind = PrimitiveType::TypeKind::Int16;
        break;
    case Token::Kind::Int32:
        type_kind = PrimitiveType::TypeKind::Int32;
        break;
    case Token::Kind::Int64:
        type_kind = PrimitiveType::TypeKind::Int64;
        break;
    case Token::Kind::Uint8:
        type_kind = PrimitiveType::TypeKind::Uint8;
        break;
    case Token::Kind::Uint16:
        type_kind = PrimitiveType::TypeKind::Uint16;
        break;
    case Token::Kind::Uint32:
        type_kind = PrimitiveType::TypeKind::Uint32;
        break;
    case Token::Kind::Uint64:
        type_kind = PrimitiveType::TypeKind::Uint64;
        break;
    case Token::Kind::Float32:
        type_kind = PrimitiveType::TypeKind::Float32;
        break;
    case Token::Kind::Float64:
        type_kind = PrimitiveType::TypeKind::Float64;
        break;
    default:
        return Fail();
    }

    ConsumeToken(Peek());
    if (!Ok())
        return Fail();
    return std::make_unique<PrimitiveType>(type_kind);
}

std::unique_ptr<RequestType> Parser::ParseRequestType() {
    ConsumeToken(Token::Kind::Request);
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::LeftAngle);
    if (!Ok())
        return Fail();
    auto identifier = ParseCompoundIdentifier();
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::RightAngle);
    if (!Ok())
        return Fail();

    auto nullability = Nullability::Nonnullable;
    if (MaybeConsumeToken(Token::Kind::Question)) {
        nullability = Nullability::Nullable;
    }

    return std::make_unique<RequestType>(std::move(identifier), nullability);
}

std::unique_ptr<Type> Parser::ParseType() {
    switch (Peek()) {
    case Token::Kind::Identifier: {
        auto identifier = ParseCompoundIdentifier();
        if (!Ok())
            return Fail();
        auto nullability = Nullability::Nonnullable;
        if (MaybeConsumeToken(Token::Kind::Question)) {
            if (!Ok())
                return Fail();
            nullability = Nullability::Nullable;
        }
        return std::make_unique<IdentifierType>(std::move(identifier), nullability);
    }

    case Token::Kind::Array: {
        auto type = ParseArrayType();
        if (!Ok())
            return Fail();
        return type;
    }

    case Token::Kind::Vector: {
        auto type = ParseVectorType();
        if (!Ok())
            return Fail();
        return type;
    }

    case Token::Kind::String: {
        auto type = ParseStringType();
        if (!Ok())
            return Fail();
        return type;
    }

    case Token::Kind::Handle: {
        auto type = ParseHandleType();
        if (!Ok())
            return Fail();
        return type;
    }

    case Token::Kind::Request: {
        auto type = ParseRequestType();
        if (!Ok())
            return Fail();
        return type;
    }

    TOKEN_PRIMITIVE_TYPE_CASES : {
        auto type = ParsePrimitiveType();
        if (!Ok())
            return Fail();
        return type;
    }

    default:
        return Fail();
    }
}

std::unique_ptr<ConstDeclaration> Parser::ParseConstDeclaration() {
    ConsumeToken(Token::Kind::Const);
    if (!Ok())
        return Fail();
    auto type = ParseType();
    if (!Ok())
        return Fail();
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::Equal);
    if (!Ok())
        return Fail();
    auto constant = ParseConstant();
    if (!Ok())
        return Fail();

    return std::make_unique<ConstDeclaration>(std::move(type), std::move(identifier),
                                              std::move(constant));
}

std::unique_ptr<EnumMember> Parser::ParseEnumMember() {
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();

    std::unique_ptr<EnumMemberValue> member_value;

    if (MaybeConsumeToken(Token::Kind::Equal)) {
        if (!Ok())
            return Fail();

        switch (Peek()) {
        case Token::Kind::Identifier: {
            auto compound_identifier = ParseCompoundIdentifier();
            if (!Ok())
                return Fail();
            member_value =
                std::make_unique<EnumMemberValueIdentifier>(std::move(compound_identifier));
            break;
        }

        case Token::Kind::NumericLiteral: {
            auto literal = ParseNumericLiteral();
            if (!Ok())
                return Fail();
            member_value = std::make_unique<EnumMemberValueNumeric>(std::move(literal));
            break;
        }

        default:
            return Fail();
        }
    }

    return std::make_unique<EnumMember>(std::move(identifier), std::move(member_value));
}

std::unique_ptr<EnumDeclaration> Parser::ParseEnumDeclaration() {
    std::vector<std::unique_ptr<EnumMember>> members;

    ConsumeToken(Token::Kind::Enum);
    if (!Ok())
        return Fail();
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();
    std::unique_ptr<PrimitiveType> subtype;
    if (MaybeConsumeToken(Token::Kind::Colon)) {
        if (!Ok())
            return Fail();
        subtype = ParsePrimitiveType();
        if (!Ok())
            return Fail();
    }
    ConsumeToken(Token::Kind::LeftCurly);
    if (!Ok())
        return Fail();

    auto parse_member = [&members, this]() {
        switch (Peek()) {
        default:
            ConsumeToken(Token::Kind::RightCurly);
            return Done;

        TOKEN_TYPE_CASES:
            members.emplace_back(ParseEnumMember());
            return More;
        }
    };

    while (parse_member() == More) {
        if (!Ok())
            Fail();
        ConsumeToken(Token::Kind::Semicolon);
        if (!Ok())
            return Fail();
    }
    if (!Ok())
        Fail();

    return std::make_unique<EnumDeclaration>(std::move(identifier), std::move(subtype),
                                             std::move(members));
}

std::unique_ptr<Parameter> Parser::ParseParameter() {
    auto type = ParseType();
    if (!Ok())
        return Fail();
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();

    return std::make_unique<Parameter>(std::move(type), std::move(identifier));
}

std::unique_ptr<ParameterList> Parser::ParseParameterList() {
    std::vector<std::unique_ptr<Parameter>> parameter_list;

    switch (Peek()) {
    default:
        break;

    TOKEN_TYPE_CASES:
        parameter_list.emplace_back(ParseParameter());
        if (!Ok())
            return Fail();
        while (Peek() == Token::Kind::Comma) {
            ConsumeToken(Token::Kind::Comma);
            if (!Ok())
                return Fail();
            switch (Peek()) {
            TOKEN_TYPE_CASES:
                parameter_list.emplace_back(ParseParameter());
                if (!Ok())
                    return Fail();
                break;

            default:
                return Fail();
            }
        }
    }

    return std::make_unique<ParameterList>(std::move(parameter_list));
}

std::unique_ptr<InterfaceMemberMethod> Parser::ParseInterfaceMemberMethod() {
    auto ordinal = ParseNumericLiteral();
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::Colon);
    if (!Ok())
        return Fail();

    std::unique_ptr<Identifier> method_name;
    std::unique_ptr<ParameterList> maybe_parameter_list;
    std::unique_ptr<ParameterList> maybe_response;

    auto parse_params = [this](std::unique_ptr<ParameterList>* params_out) {
        ConsumeToken(Token::Kind::LeftParen);
        if (!Ok())
            return false;
        *params_out = ParseParameterList();
        if (!Ok())
            return false;
        ConsumeToken(Token::Kind::RightParen);
        if (!Ok())
            return false;
        return true;
    };

    if (MaybeConsumeToken(Token::Kind::Event)) {
        method_name = ParseIdentifier();
        if (!Ok())
            return Fail();
        if (!parse_params(&maybe_response))
            return Fail();
    } else {
        method_name = ParseIdentifier();
        if (!Ok())
            return Fail();
        if (!parse_params(&maybe_parameter_list))
            return Fail();

        if (MaybeConsumeToken(Token::Kind::Arrow)) {
            if (!Ok())
                return Fail();
            if (!parse_params(&maybe_response))
                return Fail();
        }
    }

    assert(method_name);

    return std::make_unique<InterfaceMemberMethod>(std::move(ordinal),
                                                   std::move(method_name),
                                                   std::move(maybe_parameter_list),
                                                   std::move(maybe_response));
}

std::unique_ptr<InterfaceDeclaration> Parser::ParseInterfaceDeclaration() {
    std::vector<std::unique_ptr<CompoundIdentifier>> superinterfaces;
    std::vector<std::unique_ptr<ConstDeclaration>> const_members;
    std::vector<std::unique_ptr<EnumDeclaration>> enum_members;
    std::vector<std::unique_ptr<InterfaceMemberMethod>> method_members;

    ConsumeToken(Token::Kind::Interface);
    if (!Ok())
        return Fail();

    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();

    if (MaybeConsumeToken(Token::Kind::Colon)) {
        for (;;) {
            superinterfaces.emplace_back(ParseCompoundIdentifier());
            if (!Ok())
                return Fail();
            if (!MaybeConsumeToken(Token::Kind::Comma))
                break;
        }
    }

    ConsumeToken(Token::Kind::LeftCurly);
    if (!Ok())
        return Fail();

    auto parse_member = [&const_members, &enum_members, &method_members, this]() {
        switch (Peek()) {
        default:
            ConsumeToken(Token::Kind::RightCurly);
            return Done;

        case Token::Kind::Const:
            const_members.emplace_back(ParseConstDeclaration());
            return More;

        case Token::Kind::Enum:
            enum_members.emplace_back(ParseEnumDeclaration());
            return More;

        case Token::Kind::NumericLiteral:
            method_members.emplace_back(ParseInterfaceMemberMethod());
            return More;
        }
    };

    while (parse_member() == More) {
        if (!Ok())
            Fail();
        ConsumeToken(Token::Kind::Semicolon);
        if (!Ok())
            return Fail();
    }
    if (!Ok())
        Fail();

    return std::make_unique<InterfaceDeclaration>(std::move(identifier), std::move(superinterfaces),
                                                  std::move(const_members), std::move(enum_members),
                                                  std::move(method_members));
}

std::unique_ptr<StructMember> Parser::ParseStructMember() {
    auto type = ParseType();
    if (!Ok())
        return Fail();
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();

    std::unique_ptr<Constant> maybe_default_value;
    if (MaybeConsumeToken(Token::Kind::Equal)) {
        if (!Ok())
            return Fail();
        maybe_default_value = ParseConstant();
        if (!Ok())
            return Fail();
    }

    return std::make_unique<StructMember>(std::move(type), std::move(identifier),
                                          std::move(maybe_default_value));
}

std::unique_ptr<StructDeclaration> Parser::ParseStructDeclaration() {
    std::vector<std::unique_ptr<ConstDeclaration>> const_members;
    std::vector<std::unique_ptr<EnumDeclaration>> enum_members;
    std::vector<std::unique_ptr<StructMember>> members;

    ConsumeToken(Token::Kind::Struct);
    if (!Ok())
        return Fail();
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::LeftCurly);
    if (!Ok())
        return Fail();

    auto parse_member = [&const_members, &enum_members, &members, this]() {
        switch (Peek()) {
        default:
            ConsumeToken(Token::Kind::RightCurly);
            return Done;

        case Token::Kind::Const:
            const_members.emplace_back(ParseConstDeclaration());
            return More;

        case Token::Kind::Enum:
            enum_members.emplace_back(ParseEnumDeclaration());
            return More;

        TOKEN_TYPE_CASES:
            members.emplace_back(ParseStructMember());
            return More;
        }
    };

    while (parse_member() == More) {
        if (!Ok())
            Fail();
        ConsumeToken(Token::Kind::Semicolon);
        if (!Ok())
            return Fail();
    }
    if (!Ok())
        Fail();

    return std::make_unique<StructDeclaration>(std::move(identifier), std::move(const_members),
                                               std::move(enum_members), std::move(members));
}

std::unique_ptr<UnionMember> Parser::ParseUnionMember() {
    auto type = ParseType();
    if (!Ok())
        return Fail();
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();

    return std::make_unique<UnionMember>(std::move(type), std::move(identifier));
}

std::unique_ptr<UnionDeclaration> Parser::ParseUnionDeclaration() {
    std::vector<std::unique_ptr<ConstDeclaration>> const_members;
    std::vector<std::unique_ptr<EnumDeclaration>> enum_members;
    std::vector<std::unique_ptr<UnionMember>> members;

    ConsumeToken(Token::Kind::Union);
    if (!Ok())
        return Fail();
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::LeftCurly);
    if (!Ok())
        return Fail();

    auto parse_member = [&const_members, &enum_members, &members, this]() {
        switch (Peek()) {
        default:
            ConsumeToken(Token::Kind::RightCurly);
            return Done;

        case Token::Kind::Const:
            const_members.emplace_back(ParseConstDeclaration());
            return More;

        case Token::Kind::Enum:
            enum_members.emplace_back(ParseEnumDeclaration());
            return More;

        TOKEN_TYPE_CASES:
            members.emplace_back(ParseUnionMember());
            return More;
        }
    };

    while (parse_member() == More) {
        if (!Ok())
            Fail();
        ConsumeToken(Token::Kind::Semicolon);
        if (!Ok())
            return Fail();
    }
    if (!Ok())
        Fail();

    return std::make_unique<UnionDeclaration>(std::move(identifier), std::move(const_members),
                                              std::move(enum_members), std::move(members));
}

std::unique_ptr<File> Parser::ParseFile() {
    std::vector<std::unique_ptr<Using>> using_list;
    std::vector<std::unique_ptr<ConstDeclaration>> const_declaration_list;
    std::vector<std::unique_ptr<EnumDeclaration>> enum_declaration_list;
    std::vector<std::unique_ptr<InterfaceDeclaration>> interface_declaration_list;
    std::vector<std::unique_ptr<StructDeclaration>> struct_declaration_list;
    std::vector<std::unique_ptr<UnionDeclaration>> union_declaration_list;

    ConsumeToken(Token::Kind::Library);
    if (!Ok())
        return Fail();
    auto identifier = ParseCompoundIdentifier();
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::Semicolon);
    if (!Ok())
        return Fail();

    auto parse_using = [&using_list, this]() {
        switch (Peek()) {
        default:
            return Done;

        case Token::Kind::Using:
            using_list.emplace_back(ParseUsing());
            return More;
        }
    };

    while (parse_using() == More) {
        if (!Ok())
            return Fail();
        ConsumeToken(Token::Kind::Semicolon);
        if (!Ok())
            return Fail();
    }

    auto parse_declaration = [&const_declaration_list, &enum_declaration_list,
                              &interface_declaration_list, &struct_declaration_list,
                              &union_declaration_list, this]() {
        switch (Peek()) {
        default:
            return Done;

        case Token::Kind::Const:
            const_declaration_list.emplace_back(ParseConstDeclaration());
            return More;

        case Token::Kind::Enum:
            enum_declaration_list.emplace_back(ParseEnumDeclaration());
            return More;

        case Token::Kind::Interface:
            interface_declaration_list.emplace_back(ParseInterfaceDeclaration());
            return More;

        case Token::Kind::Struct:
            struct_declaration_list.emplace_back(ParseStructDeclaration());
            return More;

        case Token::Kind::Union:
            union_declaration_list.emplace_back(ParseUnionDeclaration());
            return More;
        }
    };

    while (parse_declaration() == More) {
        if (!Ok())
            return Fail();
        ConsumeToken(Token::Kind::Semicolon);
        if (!Ok())
            return Fail();
    }

    ConsumeToken(Token::Kind::EndOfFile);
    if (!Ok())
        return Fail();

    return std::make_unique<File>(
        std::move(identifier), std::move(using_list), std::move(const_declaration_list),
        std::move(enum_declaration_list), std::move(interface_declaration_list),
        std::move(struct_declaration_list), std::move(union_declaration_list));
}

} // namespace fidl
