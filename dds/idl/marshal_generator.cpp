/*
 *
 *
 * Distributed under the OpenDDS License.
 * See: http://www.opendds.org/license.html
 */

#include "marshal_generator.h"

#include "field_info.h"
#include "topic_keys.h"

#include <utl_identifier.h>

#include <string>
#include <sstream>
#include <iostream>
#include <cctype>
#include <map>

using std::string;
using namespace AstTypeClassification;

#define LENGTH(CARRAY) (sizeof(CARRAY)/sizeof(CARRAY[0]))

namespace {
  typedef bool (*is_special_case)(const string& cxx);
  typedef bool (*gen_special_case)(const string& cxx);

  typedef is_special_case is_special_sequence;
  typedef gen_special_case gen_special_sequence;

  typedef is_special_case is_special_struct;
  typedef gen_special_case gen_special_struct;

  typedef is_special_case is_special_union;
  typedef bool (*gen_special_union)(const string& cxx,
                                    AST_Type* discriminator,
                                    const std::vector<AST_UnionBranch*>& branches);

  struct special_sequence
  {
    is_special_sequence check;
    gen_special_sequence gen;
  };

  struct special_struct
  {
    is_special_struct check;
    gen_special_struct gen;
  };

  struct special_union
  {
    is_special_union check;
    gen_special_union gen;
  };

  bool isRtpsSpecialSequence(const string& cxx);
  bool genRtpsSpecialSequence(const string& cxx);

  bool isPropertySpecialSequence(const string& cxx);
  bool genPropertySpecialSequence(const string& cxx);

  bool isRtpsSpecialStruct(const string& cxx);
  bool genRtpsSpecialStruct(const string& cxx);

  bool isRtpsSpecialUnion(const string& cxx);
  bool genRtpsSpecialUnion(const string& cxx,
                           AST_Type* discriminator,
                           const std::vector<AST_UnionBranch*>& branches);

  bool isProperty_t(const string& cxx);
  bool genProperty_t(const string& cxx);

  bool isBinaryProperty_t(const string& cxx);
  bool genBinaryProperty_t(const string& cxx);

  bool isPropertyQosPolicy(const string& cxx);
  bool genPropertyQosPolicy(const string& cxx);

  bool isSecuritySubmessage(const string& cxx);
  bool genSecuritySubmessage(const string& cxx);

  string streamCommon(const string& name, AST_Type* type,
                      const string& prefix, string& intro,
                      const string& stru = "", bool printing = false);

  const special_sequence special_sequences[] = {
    {
      isRtpsSpecialSequence,
      genRtpsSpecialSequence,
    },
    {
      isPropertySpecialSequence,
      genPropertySpecialSequence,
    },
  };

  const special_struct special_structs[] = {
    {
      isRtpsSpecialStruct,
      genRtpsSpecialStruct,
    },
    {
      isProperty_t,
      genProperty_t,
    },
    {
      isBinaryProperty_t,
      genBinaryProperty_t,
    },
    {
      isPropertyQosPolicy,
      genPropertyQosPolicy,
    },
    {
      isSecuritySubmessage,
      genSecuritySubmessage,
    },
  };

  const special_union special_unions[] = {
    {
      isRtpsSpecialUnion,
      genRtpsSpecialUnion,
    },
  };

  enum Encoding {
    encoding_unaligned_cdr,
    encoding_xcdr1,
    encoding_xcdr2,
    encoding_count
  };

} /* namespace */

bool marshal_generator::gen_enum(AST_Enum*, UTL_ScopedName* name,
  const std::vector<AST_EnumVal*>& vals, const char*)
{
  NamespaceGuard ng;
  be_global->add_include("dds/DCPS/Serializer.h");
  string cxx = scoped(name); // name as a C++ class
  {
    Function insertion("operator<<", "bool");
    insertion.addArg("strm", "Serializer&");
    insertion.addArg("enumval", "const " + cxx + "&");
    insertion.endArgs();
    be_global->impl_ <<
      "  return strm << static_cast<CORBA::ULong>(enumval);\n";
  }
  {
    Function extraction("operator>>", "bool");
    extraction.addArg("strm", "Serializer&");
    extraction.addArg("enumval", cxx + "&");
    extraction.endArgs();
    be_global->impl_ <<
      "  CORBA::ULong temp = 0;\n"
      "  if (strm >> temp) {\n"
      "    if (temp >= " << vals.size() << ") {\n"
      "      strm.set_construction_status(Serializer::ElementConstructionFailure);\n"
      "      return false;\n"
      "    }\n"
      "    enumval = static_cast<" << cxx << ">(temp);\n"
      "    return true;\n"
      "  }\n"
      "  return false;\n";
  }
  return true;
}

namespace {

  string getSizeExprPrimitive(AST_Type* type,
    const string& count_expr = "count", const string& size_expr = "size",
    const string& encoding_expr = "encoding")
  {
    if (type->node_type() != AST_Decl::NT_pre_defined) {
      return "";
    }
    AST_PredefinedType* pt = dynamic_cast<AST_PredefinedType*>(type);
    const string first_args = encoding_expr + ", " + size_expr;
    switch (pt->pt()) {
    case AST_PredefinedType::PT_octet:
      return "primitive_serialized_size_octet(" + first_args + ", " + count_expr + ")";
    case AST_PredefinedType::PT_char:
      return "primitive_serialized_size_char(" + first_args + ", " + count_expr + ")";
    case AST_PredefinedType::PT_wchar:
      return "primitive_serialized_size_wchar(" + first_args + ", " + count_expr + ")";
    case AST_PredefinedType::PT_boolean:
      return "primitive_serialized_size_boolean(" + first_args + ", " + count_expr + ")";
    default:
      return "primitive_serialized_size(" + first_args + ", " +
        scoped(type->name()) + "(), " + count_expr + ")";
    }
  }

  string getSerializerName(AST_Type* type)
  {
    switch (dynamic_cast<AST_PredefinedType*>(type)->pt()) {
    case AST_PredefinedType::PT_long:
      return "long";
    case AST_PredefinedType::PT_ulong:
      return "ulong";
    case AST_PredefinedType::PT_short:
      return "short";
    case AST_PredefinedType::PT_ushort:
      return "ushort";
    case AST_PredefinedType::PT_octet:
      return "octet";
    case AST_PredefinedType::PT_char:
      return "char";
    case AST_PredefinedType::PT_wchar:
      return "wchar";
    case AST_PredefinedType::PT_float:
      return "float";
    case AST_PredefinedType::PT_double:
      return "double";
    case AST_PredefinedType::PT_longlong:
      return "longlong";
    case AST_PredefinedType::PT_ulonglong:
      return "ulonglong";
    case AST_PredefinedType::PT_longdouble:
      return "longdouble";
    case AST_PredefinedType::PT_boolean:
      return "boolean";
    default:
      return "";
    }
  }

  string nameOfSeqHeader(AST_Type* elem)
  {
    string ser = getSerializerName(elem);
    if (ser.size()) {
      ser[0] = static_cast<char>(std::toupper(ser[0]));
    }
    if (ser[0] == 'U' || ser[0] == 'W') {
      ser[1] = static_cast<char>(std::toupper(ser[1]));
    }
    const size_t fourthLast = ser.size() - 4;
    if (ser.size() > 7 && ser.substr(fourthLast) == "long") {
      ser[fourthLast] = static_cast<char>(std::toupper(ser[fourthLast]));
    }
    if (ser == "Longdouble") return "LongDouble";
    return ser;
  }

  string streamAndCheck(const string& expr, size_t indent = 2)
  {
    string idt(indent, ' ');
    return idt + "if (!(strm " + expr + ")) {\n" +
      idt + "  return false;\n" +
      idt + "}\n";
  }

  string checkAlignment(AST_Type* elem)
  {
    // At this point the stream must be 4-byte aligned (from the sequence
    // length), but it might need to be 8-byte aligned for primitives > 4.
    // (If XCDR version is < 2)
    switch (dynamic_cast<AST_PredefinedType*>(elem)->pt()) {
    case AST_PredefinedType::PT_longlong:
    case AST_PredefinedType::PT_ulonglong:
    case AST_PredefinedType::PT_double:
    case AST_PredefinedType::PT_longdouble:
      return "  encoding.align(size, 8);\n";
    default:
      return "";
    }
  }

  bool isRtpsSpecialSequence(const string& cxx)
  {
    return cxx == "OpenDDS::RTPS::ParameterList";
  }

  bool genRtpsSpecialSequence(const string& cxx)
  {
    {
      Function serialized_size("serialized_size", "void");
      serialized_size.addArg("encoding", "const Encoding&");
      serialized_size.addArg("size", "size_t&");
      serialized_size.addArg("seq", "const " + cxx + "&");
      serialized_size.endArgs();
      be_global->impl_ <<
        "  for (CORBA::ULong i = 0; i < seq.length(); ++i) {\n"
        "    if (seq[i]._d() == OpenDDS::RTPS::PID_SENTINEL) continue;\n"
        "    serialized_size(encoding, size, seq[i]);\n"
        "    OpenDDS::DCPS::align(size, 4);\n"
        "  }\n"
        "  size += 4; /* PID_SENTINEL */\n";
    }
    {
      Function insertion("operator<<", "bool");
      insertion.addArg("strm", "Serializer&");
      insertion.addArg("seq", "const " + cxx + "&");
      insertion.endArgs();
      be_global->impl_ <<
        "  for (CORBA::ULong i = 0; i < seq.length(); ++i) {\n"
        "    if (seq[i]._d() == OpenDDS::RTPS::PID_SENTINEL) continue;\n"
        "    if (!(strm << seq[i])) {\n"
        "      return false;\n"
        "    }\n"
        "  }\n"
        "  return (strm << OpenDDS::RTPS::PID_SENTINEL)\n"
        "    && (strm << OpenDDS::RTPS::PID_PAD);\n";
    }
    {
      Function extraction("operator>>", "bool");
      extraction.addArg("strm", "Serializer&");
      extraction.addArg("seq", cxx + "&");
      extraction.endArgs();
      be_global->impl_ <<
        "  while (true) {\n"
        "    const CORBA::ULong len = seq.length();\n"
        "    seq.length(len + 1);\n"
        "    if (!(strm >> seq[len])) {\n"
        "      return false;\n"
        "    }\n"
        "    if (seq[len]._d() == OpenDDS::RTPS::PID_SENTINEL) {\n"
        "      seq.length(len);\n"
        "      return true;\n"
        "    }\n"
        "  }\n";
    }
    return true;
  }

  bool isPropertySpecialSequence(const string& cxx)
  {
    return cxx == "DDS::PropertySeq"
      || cxx == "DDS::BinaryPropertySeq";
  }

  bool genPropertySpecialSequence(const string& cxx)
  {
    {
      Function serialized_size("serialized_size", "void");
      serialized_size.addArg("encoding", "const Encoding&");
      serialized_size.addArg("size", "size_t&");
      serialized_size.addArg("seq", "const " + cxx + "&");
      serialized_size.endArgs();
      be_global->impl_ <<
        "  OpenDDS::DCPS::primitive_serialized_size_ulong(encoding, size);\n"
        "  for (CORBA::ULong i = 0; i < seq.length(); ++i) {\n"
        "    serialized_size(encoding, size, seq[i]);\n"
        "  }\n";
    }
    {
      Function insertion("operator<<", "bool");
      insertion.addArg("strm", "Serializer&");
      insertion.addArg("seq", "const " + cxx + "&");
      insertion.endArgs();
      be_global->impl_ <<
        "  CORBA::ULong serlen = 0;\n"
        "  for (CORBA::ULong i = 0; i < seq.length(); ++i) {\n"
        "    if (seq[i].propagate) {\n"
        "      ++serlen;\n"
        "    }\n"
        "  }\n"
        "  if (!(strm << serlen)) {\n"
        "    return false;\n"
        "  }\n"
        "  for (CORBA::ULong i = 0; i < seq.length(); ++i) {\n"
        "    if (!(strm << seq[i])) {\n"
        "      return false;\n"
        "    }\n"
        "  }\n"
        "  return true;\n";
    }
    {
      Function extraction("operator>>", "bool");
      extraction.addArg("strm", "Serializer&");
      extraction.addArg("seq", cxx + "&");
      extraction.endArgs();
      be_global->impl_ <<
        "  CORBA::ULong length;\n"
        "  if (!(strm >> length)) {\n"
        "    return false;\n"
        "  }\n"
        "  seq.length(length);\n"
        "  for (CORBA::ULong i = 0; i < length; ++i) {\n"
        "    if (!(strm >> seq[i])) {\n"
        "      return false;\n"
        "    }\n"
        "  }\n"
        "  return true;\n";
    }
    return true;
  }

  void generate_dheader_code(const std::vector<std::string>& code, bool dheader_required, bool is_ser_func = true)
  {
    //DHeader appears on aggregated types that are mutable or appendable in XCDR2
    //DHeader also appears on ALL sequences and arrays of non-primitives
    if (dheader_required) {
      if (is_ser_func) {
        be_global->impl_ << "  size_t total_size = 0;\n";
      }
      be_global->impl_ << "  if (encoding.xcdr_version() == Encoding::XCDR_VERSION_2) {\n";
      for (size_t i = 0; i < code.size(); ++i) {
        be_global->impl_ << "    " << code[i] << "\n";
      }
      be_global->impl_ << "  }\n";
    }
  }

  void skip_to_end_sequence(string start, string end, string tempvar, bool use_cxx11, Classification cls, AST_Sequence* seq)
  {
    std::string seq_resize_func = use_cxx11 ? "resize" : "length";
    be_global->impl_ <<
      "    if (strm.encoding().xcdr_version() == Encoding::XCDR_VERSION_2) {\n"
      "      strm.skip(end_of_seq - strm.pos());\n"
      "    } else {\n"
      "      " << tempvar << " tempvar;\n"
      "      tempvar." << seq_resize_func << "(1);\n"
      "      for (CORBA::ULong j = " << start << " + 1; j < " << end << "; ++j) {\n";
    if (!use_cxx11 && (cls & CL_ARRAY)) {
      const string typedefname = scoped(seq->base_type()->name());
      be_global->impl_ <<
        "        " << typedefname << "_var tmp = " << typedefname << "_alloc();\n"
        "        " << typedefname << "_forany fa = tmp.inout();\n"
        "          strm >> fa; \n";
    } else if (cls & CL_STRING) {
      if (cls & CL_BOUNDED) {
        AST_Type* elem = resolveActualType(seq->base_type());
        const string args = string("tempvar[0]") + (use_cxx11 ? ", " : ".out(), ") + bounded_arg(elem);
        be_global->impl_ << "        strm " << ">> " << getWrapper(args, elem, WD_INPUT) << ";\n";
      } else {
        const string getbuffer =
          (be_global->language_mapping() == BE_GlobalData::LANGMAP_NONE)
          ? ".get_buffer()" : "";
        be_global->impl_ << "        strm >> tempvar" + getbuffer + "[0];\n";
      }
    } else if (use_cxx11 && (cls & (CL_ARRAY | CL_SEQUENCE))) {
      const string typedefname = scoped(seq->base_type()->name());
      const string elem_underscores = dds_generator::scoped_helper(seq->base_type()->name(), "_");
      be_global->impl_ <<
        "        strm >> IDL::DistinctType<" << typedefname << ", "  <<
          elem_underscores << "_tag>(tempvar[0]);\n";
    } else {
      be_global->impl_ << "      strm >> tempvar[0];\n";
    }
    be_global->impl_ << "      }\n"
                        "    }\n";
  }

  void skip_to_end_array()
  {
    be_global->impl_ <<
      "      if (strm.encoding().xcdr_version() == Encoding::XCDR_VERSION_2) {\n"
      "        strm.set_construction_status(Serializer::ElementConstructionFailure);\n"
      "        strm.skip(end_of_seq - strm.pos());\n"
      "        return false;\n"
      "      } else {\n"
      "        strm.set_construction_status(Serializer::ConstructionSuccessful);\n"
      "        discard_flag = true;\n"
      "      }\n";
  }

  TryConstructFailAction get_try_construct_annotation(AST_Annotation_Appl* ann_appl)
  {
    TryConstructFailAction try_construct = tryconstructfailaction_discard;
    if (ann_appl) {
      switch (get_u32_annotation_member_value(ann_appl, "value"))
      {
      case 0:
        try_construct = tryconstructfailaction_discard;
        break;
      case 1:
        try_construct = tryconstructfailaction_use_default;
        break;
      case 2:
        try_construct = tryconstructfailaction_trim;
        break;
      default:
        try_construct = tryconstructfailaction_discard;
      }
    }
    return try_construct;
  }

  void gen_sequence(UTL_ScopedName* tdname, AST_Sequence* seq)
  {
    be_global->add_include("dds/DCPS/Serializer.h");
    NamespaceGuard ng;
    string cxx = scoped(tdname);

    for (size_t i = 0; i < LENGTH(special_sequences); ++i) {
      if (special_sequences[i].check(cxx)) {
        special_sequences[i].gen(cxx);
        return;
      }
    }

    AST_Type* elem = resolveActualType(seq->base_type());
    AST_Annotation_Appl* ann_appl = seq->base_type_annotations().find("::@try_construct");
    TryConstructFailAction try_construct = get_try_construct_annotation(ann_appl);

    Classification elem_cls = classify(elem);
    const bool primitive = elem_cls & CL_PRIMITIVE;
    if (!elem->in_main_file()) {
      if (elem->node_type() == AST_Decl::NT_pre_defined) {
        if (be_global->language_mapping() != BE_GlobalData::LANGMAP_FACE_CXX &&
            be_global->language_mapping() != BE_GlobalData::LANGMAP_SP_CXX) {
          be_global->add_include(("dds/CorbaSeq/" + nameOfSeqHeader(elem)
                                  + "SeqTypeSupportImpl.h").c_str(), BE_GlobalData::STREAM_CPP);
        }
      } else {
        be_global->add_referenced(elem->file_name().c_str());
      }
    }

    const string cxx_elem = scoped(seq->base_type()->name()),
      elem_underscores = dds_generator::scoped_helper(seq->base_type()->name(), "_");
    const bool use_cxx11 = be_global->language_mapping() == BE_GlobalData::LANGMAP_CXX11;
    const string check_empty = use_cxx11 ? "seq.empty()" : "seq.length() == 0";
    const string get_length = use_cxx11 ? "static_cast<uint32_t>(seq.size())" : "seq.length()";
    const string get_buffer = use_cxx11 ? "seq.data()" : "seq.get_buffer()";
    string const_cxx = cxx, unwrap, const_unwrap;
    if (use_cxx11) {
      const string underscores = dds_generator::scoped_helper(tdname, "_");
      be_global->header_ <<
        "struct " << underscores << "_tag {};\n\n";
      unwrap = "  " + cxx + "& seq = wrap;\n  ACE_UNUSED_ARG(seq);\n";
      const_unwrap = "  const " + cxx + "& seq = wrap;\n  ACE_UNUSED_ARG(seq);\n";
      const_cxx = "IDL::DistinctType<const " + cxx + ", " + underscores + "_tag>";
      cxx = "IDL::DistinctType<" + cxx + ", " + underscores + "_tag>";
    } else {
      const_cxx = "const " + cxx + '&';
      cxx += '&';
    }

    {
      Function serialized_size("serialized_size", "void");
      serialized_size.addArg("encoding", "const Encoding&");
      serialized_size.addArg("size", "size_t&");
      serialized_size.addArg(use_cxx11 ? "wrap" : "seq", const_cxx);
      serialized_size.endArgs();

      std::vector<string> code;
      code.push_back("serialized_size_delimiter(encoding, size);");
      generate_dheader_code(code, !primitive, false);

      be_global->impl_ << const_unwrap <<
        "  OpenDDS::DCPS::primitive_serialized_size_ulong(encoding, size);\n"
        "  if (" << check_empty << ") {\n"
        "    return;\n"
        "  }\n";
      if (elem_cls & CL_ENUM) {
        be_global->impl_ <<
          "  OpenDDS::DCPS::primitive_serialized_size_ulong(encoding, size, " + get_length + ");\n";
      } else if (elem_cls & CL_PRIMITIVE) {
        be_global->impl_ << checkAlignment(elem) <<
          "  " + getSizeExprPrimitive(elem, get_length) << ";\n";
      } else if (elem_cls & CL_INTERFACE) {
        be_global->impl_ <<
          "  // sequence of objrefs is not marshaled\n";
      } else if (elem_cls == CL_UNKNOWN) {
        be_global->impl_ <<
          "  // sequence of unknown/unsupported type\n";
      } else { // String, Struct, Array, Sequence, Union
        be_global->impl_ <<
          "  for (CORBA::ULong i = 0; i < " << get_length << "; ++i) {\n";
        if (elem_cls & CL_STRING) {
          be_global->impl_ <<
            "    OpenDDS::DCPS::primitive_serialized_size_ulong(encoding, size);\n";
          const string strlen_suffix = (elem_cls & CL_WIDE)
            ? " * OpenDDS::DCPS::char16_cdr_size;\n"
            : " + 1;\n";
          if (use_cxx11) {
            be_global->impl_ <<
              "    size += seq[i].size()" << strlen_suffix;
          } else {
            be_global->impl_ <<
              "    if (seq[i]) {\n"
              "      size += ACE_OS::strlen(seq[i])" << strlen_suffix <<
              "    }\n";
          }
        } else if (!use_cxx11 && (elem_cls & CL_ARRAY)) {
          be_global->impl_ <<
            "    " << cxx_elem << "_var tmp_var = " << cxx_elem
            << "_dup(seq[i]);\n"
            "    " << cxx_elem << "_forany tmp = tmp_var.inout();\n"
            "    serialized_size(encoding, size, tmp);\n";
        } else if (use_cxx11 && (elem_cls & (CL_ARRAY | CL_SEQUENCE))) {
          be_global->impl_ <<
            "    serialized_size(encoding, size, IDL::DistinctType<const "
            << cxx_elem << ", " << elem_underscores << "_tag>(seq[i]));\n";
        } else { // Struct, Union, non-C++11 Sequence
          be_global->impl_ <<
            "    serialized_size(encoding, size, seq[i]);\n";
        }
        be_global->impl_ <<
          "  }\n";
      }
    }
    {
      Function insertion("operator<<", "bool");
      insertion.addArg("strm", "Serializer&");
      insertion.addArg(use_cxx11 ? "wrap" : "seq", const_cxx);
      insertion.endArgs();

      be_global->impl_ <<
        "  const Encoding& encoding = strm.encoding();\n"
        "  ACE_UNUSED_ARG(encoding);\n";
      std::vector<string> code;
      code.push_back(string("serialized_size(strm.encoding(), total_size, ") + (use_cxx11 ? "wrap" : "seq") + ");");
      code.push_back("if (!strm.write_delimiter(total_size)) {");
      code.push_back("  return false;");
      code.push_back("}");
      generate_dheader_code(code, !primitive);

      be_global->impl_ << const_unwrap <<
        "  const CORBA::ULong length = " << get_length << ";\n";
      if (!seq->unbounded()) {
        be_global->impl_ <<
          "  if (length > " << bounded_arg(seq) << ") {\n"
          "    return false;\n"
          "  }\n";
      }
      be_global->impl_ <<
        streamAndCheck("<< length") <<
        "  if (length == 0) {\n"
        "    return true;\n"
        "  }\n";
      if (elem_cls & CL_PRIMITIVE) {
        AST_PredefinedType* predef = dynamic_cast<AST_PredefinedType*>(elem);
        if (use_cxx11 && predef->pt() == AST_PredefinedType::PT_boolean) {
          be_global->impl_ <<
            "  for (CORBA::ULong i = 0; i < length; ++i) {\n" <<
            streamAndCheck("<< ACE_OutputCDR::from_boolean(seq[i])", 4) <<
            "  }\n"
            "  return true;\n";
        } else {
          be_global->impl_ <<
            "  return strm.write_" << getSerializerName(elem)
            << "_array(" << get_buffer << ", length);\n";
        }
      } else if (elem_cls & CL_INTERFACE) {
        be_global->impl_ <<
          "  return false; // sequence of objrefs is not marshaled\n";
      } else if (elem_cls == CL_UNKNOWN) {
        be_global->impl_ <<
          "  return false; // sequence of unknown/unsupported type\n";
      } else { // Enum, String, Struct, Array, Sequence, Union
        be_global->impl_ <<
          "  for (CORBA::ULong i = 0; i < length; ++i) {\n";
        if (!use_cxx11 && (elem_cls & CL_ARRAY)) {
          const string typedefname = scoped(seq->base_type()->name());
          be_global->impl_ <<
            "    " << typedefname << "_var tmp_var = " << typedefname
            << "_dup(seq[i]);\n"
            "    " << typedefname << "_forany tmp = tmp_var.inout();\n"
            << streamAndCheck("<< tmp", 4);
        } else if ((elem_cls & (CL_STRING | CL_BOUNDED)) == (CL_STRING | CL_BOUNDED)) {
          const string args = "seq[i], " + bounded_arg(elem);
          be_global->impl_ <<
            streamAndCheck("<< " + getWrapper(args, elem, WD_OUTPUT), 4);
        } else if (use_cxx11 && (elem_cls & (CL_ARRAY | CL_SEQUENCE))) {
          be_global->impl_ <<
            streamAndCheck("<< IDL::DistinctType<const " + cxx_elem + ", " +
                           elem_underscores + "_tag>(seq[i])", 4);
        } else {
          be_global->impl_ << streamAndCheck("<< seq[i]", 4);
        }
        be_global->impl_ <<
          "  }\n"
          "  return true;\n";
      }
    }
    {
      Function extraction("operator>>", "bool");
      extraction.addArg("strm", "Serializer&");
      extraction.addArg(use_cxx11 ? "wrap" : "seq", cxx);
      extraction.endArgs();

      be_global->impl_ <<
        "  const Encoding& encoding = strm.encoding();\n"
        "  ACE_UNUSED_ARG(encoding);\n";
      std::vector<string> code;
      code.push_back("if (!strm.read_delimiter(total_size)) {");
      code.push_back("  return false;");
      code.push_back("}");
      generate_dheader_code(code, !primitive);
      if (!primitive) {
        be_global->impl_ << "  const size_t end_of_seq = strm.pos() + total_size;\n";
      }
      be_global->impl_ << unwrap <<
        "  CORBA::ULong length;\n"
        << streamAndCheck(">> length");
      AST_PredefinedType* predef = dynamic_cast<AST_PredefinedType*>(elem);
      string bound;
      if (!seq->unbounded()) {
        bound = (use_cxx11 ? bounded_arg(seq) : "seq.maximum()");
      }
      //create a variable called newlength which tells us how long we need to copy to
      //for an unbounded sequence this is just our length
      //for a bounded sequence this is our maximum
      //we save the old length so we know how far we need to read until
      be_global->impl_ << "  CORBA::ULong new_length = length;\n";
      if (elem_cls & CL_PRIMITIVE) {
        // if we are a bounded primitive, we read to our max then return false
        if (!seq->unbounded()) {
          be_global->impl_ <<
            "  if (length > " << bound << ") {\n"
            "    new_length = " << bound << ";\n"
            "  }\n";
          be_global->impl_ <<
            (use_cxx11 ? "  wrap.val_->resize(" : "  seq.length(") << "new_length);\n";
          if (use_cxx11 && predef->pt() == AST_PredefinedType::PT_boolean) {
            be_global->impl_ <<
            "  for (CORBA::ULong i = 0; i < new_length; ++i) {\n"
            "    bool b;\n" <<
            streamAndCheck(">> ACE_InputCDR::to_boolean(b)", 4) <<
            "    seq[i] = b;\n"
            "  }\n";
          } else {
            be_global->impl_ <<
              "  strm.read_" << getSerializerName(elem) << "_array(" << get_buffer << ", new_length);\n";
          }
          be_global->impl_ <<
            "  if (new_length != length) {\n"
            "    size_t skip_length = 0;\n"
            "    " << getSizeExprPrimitive(elem, "(length - new_length)", "skip_length", "strm.encoding()") << ";\n"
            "    strm.set_construction_status(Serializer::BoundConstructionFailure);\n"
            "    strm.skip(skip_length);\n"
            "    return false;\n"
            "  }\n";
        } else {
          be_global->impl_ <<
            (use_cxx11 ? "  wrap.val_->resize(new_length);\n" : "  seq.length(new_length);\n");
          if (use_cxx11 && predef->pt() == AST_PredefinedType::PT_boolean) {
            be_global->impl_ <<
              "  for (CORBA::ULong i = 0; i < length; ++i) {\n"
              "    bool b;\n" <<
              streamAndCheck(">> ACE_InputCDR::to_boolean(b)", 4) <<
              "    seq[i] = b;\n"
              "  }\n"
              "  return true;\n";
          } else {
            be_global->impl_ <<
              "  if (length == 0) {\n"
              "    return true;\n"
              "  }\n"
              "  return strm.read_" << getSerializerName(elem)
              << "_array(" << get_buffer << ", length);\n";
          }
        }
      } else if (elem_cls & CL_INTERFACE) {
        be_global->impl_ <<
          "  return false; // sequence of objrefs is not marshaled\n";
        return;
      } else if (elem_cls == CL_UNKNOWN) {
        be_global->impl_ <<
          "  return false; // sequence of unknown/unsupported type\n";
        return;
      } else { // Enum, String, Struct, Array, Sequence, Union
        if (!seq->unbounded()) {
          be_global->impl_ <<
            "  if (length > " << (use_cxx11 ? bounded_arg(seq) : "seq.maximum()") << ") {\n"
            "    new_length = " << bound << ";\n"
            "  }\n";
        }
        //change the size of seq length to prepare
        be_global->impl_ <<
          (use_cxx11 ? "  wrap.val_->resize(new_length);\n" : "  seq.length(new_length);\n");
        //read the entire length of the writer's sequence
        be_global->impl_ <<
          "  for (CORBA::ULong i = 0; i < new_length; ++i) {\n";

        if (!use_cxx11 && (elem_cls & CL_ARRAY)) {
          const string typedefname = scoped(seq->base_type()->name());
          be_global->impl_ <<
            "      " << typedefname << "_var tmp = " << typedefname
            << "_alloc();\n"
            "      " << typedefname << "_forany fa = tmp.inout();\n"
            << "    if (!(strm >> fa)) {\n";
        } else if (elem_cls & CL_STRING) {
          if (elem_cls & CL_BOUNDED) {
            const string args = string("seq[i]") + (use_cxx11 ? ", " : ".out(), ") + bounded_arg(elem);
            be_global->impl_ << "    if (!(strm " << ">> " << getWrapper(args, elem, WD_INPUT) << ")) {\n";
          } else {
            const string getbuffer =
              (be_global->language_mapping() == BE_GlobalData::LANGMAP_NONE)
              ? ".get_buffer()" : "";
            be_global->impl_ << "if (!(strm >> seq" + getbuffer + "[i])) {\n";
          }
        } else if (use_cxx11 && (elem_cls & (CL_ARRAY | CL_SEQUENCE))) {
          be_global->impl_ <<
          "      if (!(strm >> IDL::DistinctType<" << cxx_elem << ", "  <<
                           elem_underscores << "_tag>(seq[i])" << ")) {\n";
        } else {
          be_global->impl_ << "   if (!(strm >> seq[i])) {\n";
        }
        std::string seq_resize_func = use_cxx11 ? "resize" : "length";

        if (try_construct == tryconstructfailaction_use_default) {
          be_global->impl_ << "     " << type_to_default(elem, "seq[i]") <<
                              "           strm.set_construction_status(Serializer::ConstructionSuccessful);\n";
        } else if ((try_construct == tryconstructfailaction_trim) && (elem_cls & CL_BOUNDED) &&
                   ((elem_cls & CL_STRING) || (elem_cls & CL_SEQUENCE))) {
          if (elem_cls & CL_STRING){
            string check_not_empty = use_cxx11 ? "!seq[i].empty()" : "seq[i].in()";
            string get_length = use_cxx11 ? "seq[i].length()" : "ACE_OS::strlen(seq[i].in())";
            string inout = use_cxx11 ? "" : ".inout()";
            if (use_cxx11){
              be_global->impl_ <<
                "      if (strm.good_bit() && " << check_not_empty << " && ("
                        << bounded_arg(elem) << " < " << get_length << ")) {\n"
                "        seq[i]" << inout <<inout << ".resize(" << bounded_arg(elem) <<  ");\n"
                "      }";
            } else {
              be_global->impl_ <<
                "      if (strm.good_bit() && " << check_not_empty << " && ("
                        << bounded_arg(elem) << " < " << get_length << ")) {\n"
                "        seq[i]" << inout << "[" << bounded_arg(elem) << "] = 0;\n"
                "      }";
            }
            be_global->impl_ <<
              "  else {\n"
              "        strm.set_construction_status(Serializer::ElementConstructionFailure);\n"
              "        ";
            skip_to_end_sequence("i", "length", scoped(tdname), use_cxx11, elem_cls, seq);
            be_global->impl_ <<   "        return false;\n"
              "      }\n";
          } else if (elem_cls & CL_SEQUENCE) {
            be_global->impl_ << "      if(strm.get_construction_status() == Serializer::ElementConstructionFailure) {\n";
            skip_to_end_sequence("i", "length", scoped(tdname), use_cxx11, elem_cls, seq);
            be_global->impl_ << "        return false;\n"
              "      }\n"
              "      strm.set_construction_status(Serializer::ConstructionSuccessful);\n";
          }
        } else {
          //discard/default
          be_global->impl_ <<
          "      strm.set_construction_status(Serializer::ElementConstructionFailure);\n"
          "      ";
          skip_to_end_sequence("i", "length", scoped(tdname), use_cxx11, elem_cls, seq);
          be_global->impl_ << "      return false;\n";
        }
        be_global->impl_ << "    }\n";
        if (!use_cxx11 && (elem_cls & CL_ARRAY)) {
          be_global->impl_ << "    " << scoped(seq->base_type()->name()) << "_copy(seq[i], tmp.in());\n";
        }
        be_global->impl_ << "  }\n";
      }
      if (!primitive) {
        be_global->impl_ << "  if (new_length != length) {\n";
        skip_to_end_sequence("new_length", "length", scoped(tdname), use_cxx11, elem_cls, seq);
        be_global->impl_ << "    strm.set_construction_status(Serializer::BoundConstructionFailure);\n"
          "    return false;\n"
          "  }\n";
      }
      be_global->impl_ << "  return true;\n";
    }
  }

  void gen_anonymous_sequence(const FieldInfo& sf)
  {
    string cxx = sf.name_;
    if (!sf.as_act_->in_main_file()) {
      if (sf.as_act_->node_type() == AST_Decl::NT_pre_defined) {
        if (be_global->language_mapping() != BE_GlobalData::LANGMAP_FACE_CXX &&
            be_global->language_mapping() != BE_GlobalData::LANGMAP_SP_CXX) {
          be_global->add_include(("dds/CorbaSeq/" + nameOfSeqHeader(sf.as_act_)
                                  + "SeqTypeSupportImpl.h").c_str(), BE_GlobalData::STREAM_CPP);
        }
      } else {
        be_global->add_referenced(sf.as_act_->file_name().c_str());
      }
    }

    const bool use_cxx11 = be_global->language_mapping() == BE_GlobalData::LANGMAP_CXX11;
    const bool primitive = sf.as_cls_ & CL_PRIMITIVE;
    const string check_empty = use_cxx11 ? "seq.empty()" : "seq.length() == 0";
    const string get_length = use_cxx11 ? "static_cast<uint32_t>(seq.size())" : "seq.length()";
    const string get_buffer = use_cxx11 ? "wrap.val_->data()" : "seq.get_buffer()";
    string const_cxx = cxx;
    if (use_cxx11) {
      be_global->header_ << "struct " << sf.underscored_ << "_tag {};\n\n";
      const_cxx = "IDL::DistinctType<const " + cxx + ", " + sf.underscored_ + "_tag>";
    } else {
      const_cxx = "const " + cxx + '&';
    }
    {
      Function serialized_size("serialized_size", "void");
      serialized_size.addArg("encoding", "const Encoding&");
      serialized_size.addArg("size", "size_t&");
      serialized_size.addArg(sf.arg_.c_str(), sf.const_ref_);
      serialized_size.endArgs();

      std::vector<string> code;
      code.push_back("serialized_size_delimiter(encoding, size);");
      generate_dheader_code(code, !primitive, false);

      be_global->impl_ << sf.const_unwrap_ <<
        "  OpenDDS::DCPS::primitive_serialized_size_ulong(encoding, size);\n"
        "  if (" << check_empty << ") {\n"
        "    return;\n"
        "  }\n";
      if (sf.as_cls_ & CL_ENUM) {
        be_global->impl_ <<
          "  OpenDDS::DCPS::primitive_serialized_size_ulong(encoding, size, " + get_length + ");\n";
      } else if (sf.as_cls_ & CL_PRIMITIVE) {
        be_global->impl_ << checkAlignment(sf.as_act_) <<
          "  " + getSizeExprPrimitive(sf.as_act_, get_length) << ";\n";
      } else if (sf.as_cls_ & CL_INTERFACE) {
        be_global->impl_ <<
          "  // sequence of objrefs is not marshaled\n";
      } else if (sf.as_cls_ == CL_UNKNOWN) {
        be_global->impl_ <<
          "  // sequence of unknown/unsupported type\n";
      } else { // String, Struct, Array, Sequence, Union
        be_global->impl_ <<
          "  for (CORBA::ULong i = 0; i < " << get_length << "; ++i) {\n";
        if (sf.as_cls_ & CL_STRING) {
          be_global->impl_ <<
            "    OpenDDS::DCPS::primitive_serialized_size_ulong(encoding, size);\n";
          const string strlen_suffix = (sf.as_cls_ & CL_WIDE)
            ? " * OpenDDS::DCPS::char16_cdr_size;\n"
            : " + 1;\n";
          if (use_cxx11) {
            be_global->impl_ <<
              "    size += seq[i].size()" << strlen_suffix;
          } else {
            be_global->impl_ <<
              "    if (seq[i]) {\n"
              "      size += ACE_OS::strlen(seq[i])" << strlen_suffix <<
              "    }\n";
          }
        } else if (!use_cxx11 && (sf.as_cls_ & CL_ARRAY)) {
          be_global->impl_ <<
            "    " << sf.scoped_elem_ << "_var tmp_var = " << sf.scoped_elem_ << "_dup(seq[i]);\n"
            "    " << sf.scoped_elem_ << "_forany tmp = tmp_var.inout();\n"
            "    serialized_size(encoding, size, tmp);\n";
        } else if (use_cxx11 && (sf.as_cls_ & (CL_ARRAY | CL_SEQUENCE))) {
          be_global->impl_ <<
            "    serialized_size(encoding, size, " << sf.elem_const_ref_ + "(seq[i]));\n";
        } else { // Struct, Union, non-C++11 Sequence
          be_global->impl_ <<
            "    serialized_size(encoding, size, seq[i]);\n";
        }
        be_global->impl_ <<
          "  }\n";
      }
    }
    {
      Function insertion("operator<<", "bool");
      insertion.addArg("strm", "Serializer&");
      insertion.addArg(sf.arg_.c_str(), sf.const_ref_);
      insertion.endArgs();

      be_global->impl_ << "  const Encoding& encoding = strm.encoding();\n";
      std::vector<string> code;
      code.push_back(string("serialized_size(strm.encoding(), total_size, ") + (use_cxx11 ? "wrap" : "seq") + ");");
      code.push_back("if (!strm.write_delimiter(total_size)) {");
      code.push_back("  return false;");
      code.push_back("}");
      generate_dheader_code(code, !primitive);

      be_global->impl_ << sf.const_unwrap_ <<
        "  const CORBA::ULong length = " << get_length << ";\n";
      if (!sf.seq_->unbounded()) {
        be_global->impl_ <<
          "  if (length > " << bounded_arg(sf.seq_) << ") {\n"
          "    return false;\n"
          "  }\n";
      }
      be_global->impl_ <<
        streamAndCheck("<< length") <<
        "  if (length == 0) {\n"
        "    return true;\n"
        "  }\n";
      if (sf.as_cls_ & CL_PRIMITIVE) {
        AST_PredefinedType* predef = dynamic_cast<AST_PredefinedType*>(sf.as_act_);
        if (use_cxx11 && predef->pt() == AST_PredefinedType::PT_boolean) {
          be_global->impl_ <<
            "  for (CORBA::ULong i = 0; i < length; ++i) {\n" <<
            streamAndCheck("<< ACE_OutputCDR::from_boolean(seq[i])", 4) <<
            "  }\n"
            "  return true;\n";
        } else {
          be_global->impl_ <<
            "  return strm.write_" << getSerializerName(sf.as_act_)
            << "_array(" << get_buffer << ", length);\n";
        }
      } else if (sf.as_cls_ & CL_INTERFACE) {
        be_global->impl_ <<
          "  return false; // sequence of objrefs is not marshaled\n";
      } else if (sf.as_cls_ == CL_UNKNOWN) {
        be_global->impl_ <<
          "  return false; // sequence of unknown/unsupported type\n";
      } else { // Enum, String, Struct, Array, Sequence, Union
        be_global->impl_ <<
          "  for (CORBA::ULong i = 0; i < length; ++i) {\n";
        if (!use_cxx11 && (sf.as_cls_ & CL_ARRAY)) {
          be_global->impl_ <<
            "    " << sf.scoped_elem_ << "_var tmp_var = " << sf.scoped_elem_ << "_dup(seq[i]);\n"
            "    " << sf.scoped_elem_ << "_forany tmp = tmp_var.inout();\n"
            << streamAndCheck("<< tmp", 4);
        } else if ((sf.as_cls_ & (CL_STRING | CL_BOUNDED)) == (CL_STRING | CL_BOUNDED)) {
          const string args = "seq[i], " + bounded_arg(sf.as_act_);
          be_global->impl_ <<
            streamAndCheck("<< " + getWrapper(args, sf.as_act_, WD_OUTPUT), 4);
        } else if (use_cxx11 && (sf.as_cls_ & (CL_ARRAY | CL_SEQUENCE))) {
          be_global->impl_ <<
            streamAndCheck("<< " + sf.elem_const_ref_ + "(seq[i])", 4);
        } else {
          be_global->impl_ << streamAndCheck("<< seq[i]", 4);
        }
        be_global->impl_ <<
          "  }\n"
          "  return true;\n";
      }
    }
    {
      Function extraction("operator>>", "bool");
      extraction.addArg("strm", "Serializer&");
      extraction.addArg(sf.arg_.c_str(), sf.ref_);
      extraction.endArgs();

      be_global->impl_ << "  const Encoding& encoding = strm.encoding();\n";
      std::vector<string> code;
      code.push_back("if (!strm.read_delimiter(total_size)) {");
      code.push_back("  return false;");
      code.push_back("}");
      generate_dheader_code(code, !primitive);

      const string cxx_elem = scoped(sf.seq_->base_type()->name()),
        elem_underscores = dds_generator::scoped_helper(sf.seq_->base_type()->name(), "_");
      AST_Annotation_Appl* ann_appl = sf.seq_->base_type_annotations().find("::@try_construct");
      TryConstructFailAction try_construct = get_try_construct_annotation(ann_appl);

      if (!primitive) {
        be_global->impl_ << "  const size_t end_of_seq = strm.pos() + total_size;\n";
      }
      be_global->impl_ << sf.unwrap_ <<
        "  CORBA::ULong length;\n"
        << streamAndCheck(">> length");
      AST_PredefinedType* predef = dynamic_cast<AST_PredefinedType*>(sf.as_act_);
      string bound;
      if (!sf.seq_->unbounded()) {
        bound = (use_cxx11 ? bounded_arg(sf.seq_) : "seq.maximum()");
      }
      be_global->impl_ << "  CORBA::ULong new_length = length;\n";
      if (sf.as_cls_ & CL_PRIMITIVE) {
        if (!sf.seq_->unbounded()) {
          be_global->impl_ <<
            "  if (length > " << bound << ") {\n"
            "    new_length = " << bound << ";\n"
            "  }\n";
          be_global->impl_ <<
            (use_cxx11 ? "  wrap.val_->resize(" : "  seq.length(") << "new_length);\n";
          if (use_cxx11 && predef->pt() == AST_PredefinedType::PT_boolean) {
            be_global->impl_ <<
            "  for (CORBA::ULong i = 0; i < new_length; ++i) {\n"
            "    bool b;\n" <<
            streamAndCheck(">> ACE_InputCDR::to_boolean(b)", 4) <<
            "    seq[i] = b;\n"
            "  }\n";
          } else {
            be_global->impl_ <<
              "  strm.read_" << getSerializerName(sf.as_act_) << "_array(" << get_buffer << ", new_length);\n";
          }
          be_global->impl_ <<
            "  if (new_length != length) {\n"
            "    size_t skip_length = 0;\n"
            "    " << getSizeExprPrimitive(sf.as_act_, "(length - new_length)", "skip_length", "strm.encoding()") << ";\n"
            "    strm.set_construction_status(Serializer::BoundConstructionFailure);\n"
            "    strm.skip(skip_length);\n"
            "    return false;\n"
            "  }\n";
        } else {
          be_global->impl_ <<
            (use_cxx11 ? "  wrap.val_->resize(new_length);\n" : "  seq.length(new_length);\n");
          if (use_cxx11 && predef->pt() == AST_PredefinedType::PT_boolean) {
            be_global->impl_ <<
              "  for (CORBA::ULong i = 0; i < length; ++i) {\n"
              "    bool b;\n" <<
              streamAndCheck(">> ACE_InputCDR::to_boolean(b)", 4) <<
              "    seq[i] = b;\n"
              "  }\n"
              "  return true;\n";
          } else {
            be_global->impl_ <<
              "  if (length == 0) {\n"
              "    return true;\n"
              "  }\n"
              "  return strm.read_" << getSerializerName(sf.as_act_)
              << "_array(" << get_buffer << ", length);\n";
          }
        }
      } else if (sf.as_cls_ & CL_INTERFACE) {
        be_global->impl_ <<
          "  return false; // sequence of objrefs is not marshaled\n";
      } else if (sf.as_cls_ == CL_UNKNOWN) {
        be_global->impl_ <<
          "  return false; // sequence of unknown/unsupported type\n";
      } else { // Enum, String, Struct, Array, Sequence, Union
        if (!sf.seq_->unbounded()) {
          be_global->impl_ <<
            "  if (length > " << (use_cxx11 ? bounded_arg(sf.seq_) : "seq.maximum()") << ") {\n"
            "    new_length = " << bound << ";\n"
            "  }\n";
        }
        //change the size of seq length to prepare
        be_global->impl_ <<
          (use_cxx11 ? "  wrap.val_->resize(new_length);\n" : "  seq.length(new_length);\n");
        //read the entire length of the writer's sequence
        be_global->impl_ <<
          "  for (CORBA::ULong i = 0; i < new_length; ++i) {\n";
        if (!use_cxx11 && (sf.as_cls_ & CL_ARRAY)) {
          const string typedefname = scoped(sf.seq_->base_type()->name());
          be_global->impl_ <<
            "      " << typedefname << "_var tmp = " << typedefname
            << "_alloc();\n"
            "      " << typedefname << "_forany fa = tmp.inout();\n"
            << "    if (!(strm >> fa)) {\n";
        } else if (sf.as_cls_ & CL_STRING) {
          if (sf.as_cls_ & CL_BOUNDED) {
            const string args = string("seq[i]") + (use_cxx11 ? ", " : ".out(), ") + bounded_arg(sf.as_act_);
            be_global->impl_ << "    if (!(strm " << ">> " << getWrapper(args, sf.as_act_, WD_INPUT) << ")) {\n";
          } else {
            const string getbuffer =
              (be_global->language_mapping() == BE_GlobalData::LANGMAP_NONE)
              ? ".get_buffer()" : "";
            be_global->impl_ << "    if (!(strm >> seq" + getbuffer + "[i])) {\n";
          }
        } else if (use_cxx11 && (sf.as_cls_ & (CL_ARRAY | CL_SEQUENCE))) {
          be_global->impl_ <<
            "    if (!(strm >> " << sf.elem_ref_ << "(seq[i])" << ")) {\n";
        } else {
          be_global->impl_ << "   if (!(strm >> seq[i])) {\n";
        }

        if (try_construct == tryconstructfailaction_use_default) {
          be_global->impl_ <<
            "     " << type_to_default(sf.as_base_, "seq[i]") <<
            "           strm.set_construction_status(Serializer::ConstructionSuccessful);\n";
        } else if ((try_construct == tryconstructfailaction_trim) && (sf.as_cls_ & CL_BOUNDED) &&
                   ((sf.as_cls_ & CL_STRING) || (sf.as_cls_ & CL_SEQUENCE))) {
          if (sf.as_cls_ & CL_STRING){
            string check_not_empty = use_cxx11 ? "!seq[i].empty()" : "seq[i].in()";
            string get_length = use_cxx11 ? "seq[i].length()" : "ACE_OS::strlen(seq[i].in())";
            string inout = use_cxx11 ? "" : ".inout()";
            if (use_cxx11){
              be_global->impl_ <<
                "        if (strm.good_bit() && " << check_not_empty << " && (" <<
                bounded_arg(sf.as_act_) << " < " << get_length << ")) {\n"
                "          seq[i]" << inout << ".resize(" << bounded_arg(sf.as_act_) <<  ");\n"
                "        }";
            } else {
              be_global->impl_ <<
                "        if (strm.good_bit() && " << check_not_empty << " && (" <<
                bounded_arg(sf.as_act_) << " < " << get_length << ")) {\n"
                "          seq[i]" << inout << "[" << bounded_arg(sf.as_act_) << "] = 0;\n"
                "        }";
            }
            be_global->impl_ <<
              "      else {\n"
              "        strm.set_construction_status(Serializer::ElementConstructionFailure);\n"
              "        ";
            skip_to_end_sequence("i", "length", sf.scoped_type_, use_cxx11, sf.as_cls_, sf.seq_);
            be_global->impl_ <<
              "        return false;\n"
              "      }\n";
          } else if (sf.as_cls_ & CL_SEQUENCE) {
            be_global->impl_ << "      if(strm.get_construction_status() == Serializer::ElementConstructionFailure) {\n";
            skip_to_end_sequence("i", "length", sf.scoped_type_, use_cxx11, sf.as_cls_, sf.seq_);
            be_global->impl_ <<
              "        return false;\n"
              "      }\n"
              "      strm.set_construction_status(Serializer::ConstructionSuccessful);\n";
          }
        } else {
          //discard/default
          be_global->impl_ <<
            "      strm.set_construction_status(Serializer::ElementConstructionFailure);\n"
            "      ";
          skip_to_end_sequence("i", "length", sf.scoped_type_, use_cxx11, sf.as_cls_, sf.seq_);
          be_global->impl_ << "      return false;\n";
        }
        be_global->impl_ << "    }\n";
        if (!use_cxx11 && (sf.as_cls_ & CL_ARRAY)) {
          be_global->impl_ << "    " << scoped(sf.seq_->base_type()->name()) << "_copy(seq[i], tmp.in());\n";
        }
        be_global->impl_ << "  }\n";
      }
      if (!primitive) {
        be_global->impl_ << "  if (new_length != length) {\n";
        skip_to_end_sequence("new_length", "length", sf.scoped_type_, use_cxx11, sf.as_cls_, sf.seq_);
        be_global->impl_ <<
          "    strm.set_construction_status(Serializer::BoundConstructionFailure);\n"
          "    return false;\n"
          "  }\n";
      }
      be_global->impl_ << "  return true;\n";
    }
  }

  void gen_array(UTL_ScopedName* name, AST_Array* arr)
  {
    be_global->add_include("dds/DCPS/Serializer.h");
    NamespaceGuard ng;
    const bool use_cxx11 = be_global->language_mapping() == BE_GlobalData::LANGMAP_CXX11;
    string cxx = scoped(name);
    string const_cxx = cxx, unwrap, const_unwrap;
    if (use_cxx11) {
      const string underscores = dds_generator::scoped_helper(name, "_");
      be_global->header_ <<
        "struct " << underscores << "_tag {};\n\n";
      unwrap = "  " + cxx + "& arr = wrap;\n  ACE_UNUSED_ARG(arr);\n";
      const_unwrap = "  const " + cxx + "& arr = wrap;\n  ACE_UNUSED_ARG(arr);\n";
      const_cxx = "IDL::DistinctType<const " + cxx + ", " + underscores + "_tag>";
      cxx = "IDL::DistinctType<" + cxx + ", " + underscores + "_tag>";
    } else {
      const_cxx = "const " + cxx + "_forany&";
      cxx += "_forany&";
    }

    AST_Type* elem = resolveActualType(arr->base_type());
    AST_Annotation_Appl* ann_appl = arr->base_type_annotations().find("::@try_construct");
    TryConstructFailAction try_construct = get_try_construct_annotation(ann_appl);
    Classification elem_cls = classify(elem);
    const bool primitive = elem_cls & CL_PRIMITIVE;
    if (!elem->in_main_file()
        && elem->node_type() != AST_Decl::NT_pre_defined) {
      be_global->add_referenced(elem->file_name().c_str());
    }
    string cxx_elem = scoped(arr->base_type()->name());
    size_t n_elems = 1;
    for (size_t i = 0; i < arr->n_dims(); ++i) {
      n_elems *= arr->dims()[i]->ev()->u.ulval;
    }
    {
      Function set_default("set_default", "void");
      set_default.addArg("stru", cxx);
      set_default.endArgs();
      string indent = "  ";
      string var_name = "stru";
      if (use_cxx11) {
        be_global->impl_ << "  " << scoped(arr->name()) + "& arr = stru;\n";
        var_name = "arr";
      }
      NestedForLoops nfl("CORBA::ULong", "i", arr, indent);
      be_global->impl_ << "    " << type_to_default(elem, var_name + nfl.index_);
    }
    {
      Function serialized_size("serialized_size", "void");
      serialized_size.addArg("encoding", "const Encoding&");
      serialized_size.addArg("size", "size_t&");
      serialized_size.addArg(use_cxx11 ? "wrap" : "arr", const_cxx);
      serialized_size.endArgs();

      std::vector<string> code;
      code.push_back("serialized_size_delimiter(encoding, size);");
      generate_dheader_code(code, !primitive, false);

      be_global->impl_ << const_unwrap;
      if (elem_cls & CL_ENUM) {
        be_global->impl_ <<
          "  OpenDDS::DCPS::primitive_serialized_size_ulong(encoding, size);\n";
        if (n_elems > 1) {
          be_global->impl_ <<
            "  OpenDDS::DCPS::primitive_serialized_size_ulong(encoding, size, "
              << n_elems - 1 << ");\n";
        }
      } else if (elem_cls & CL_PRIMITIVE) {
        std::ostringstream n_elems_ss;
        n_elems_ss << n_elems;
        be_global->impl_ <<
          "  " << getSizeExprPrimitive(elem, n_elems_ss.str()) << ";\n";
      } else { // String, Struct, Array, Sequence, Union
        string indent = "  ";
        NestedForLoops nfl("CORBA::ULong", "i", arr, indent);
        if (elem_cls & CL_STRING) {
          be_global->impl_ <<
            indent << "OpenDDS::DCPS::primitive_serialized_size_ulong(encoding, size);\n" <<
            indent;
          if (use_cxx11) {
            be_global->impl_ << "size += arr" << nfl.index_ << ".size()";
          } else {
            be_global->impl_ << "size += ACE_OS::strlen(arr" << nfl.index_ << ".in())";
          }
          be_global->impl_ << ((elem_cls & CL_WIDE)
            ? " * OpenDDS::DCPS::char16_cdr_size;\n"
            : " + 1;\n");
        } else if (!use_cxx11 && (elem_cls & CL_ARRAY)) {
          be_global->impl_ <<
            indent << cxx_elem << "_var tmp_var = " << cxx_elem
            << "_dup(arr" << nfl.index_ << ");\n" <<
            indent << cxx_elem << "_forany tmp = tmp_var.inout();\n" <<
            indent << "serialized_size(encoding, size, tmp);\n";
        } else { // Struct, Sequence, Union, C++11 Array
          string pre, post;
          if (use_cxx11 && (elem_cls & (CL_ARRAY | CL_SEQUENCE))) {
            pre = "IDL::DistinctType<const " + cxx_elem + ", " +
              dds_generator::scoped_helper(arr->base_type()->name(), "_") + "_tag>(";
            post = ')';
          }
          be_global->impl_ <<
            indent << "serialized_size(encoding, size, "
              << pre << "arr" << nfl.index_ << post << ");\n";
        }
      }
    }
    {
      Function insertion("operator<<", "bool");
      insertion.addArg("strm", "Serializer&");
      insertion.addArg(use_cxx11 ? "wrap" : "arr", const_cxx);
      insertion.endArgs();

      be_global->impl_ <<
        "  const Encoding& encoding = strm.encoding();\n"
        "  ACE_UNUSED_ARG(encoding);\n";
      std::vector<string> code;
      code.push_back(string("serialized_size(strm.encoding(), total_size, ") + (use_cxx11 ? "wrap" : "arr") + ");");
      code.push_back("if (!strm.write_delimiter(total_size)) {");
      code.push_back("  return false;");
      code.push_back("}");
      generate_dheader_code(code, !primitive);
      be_global->impl_ << const_unwrap;
      const std::string accessor = use_cxx11 ? ".data()" : ".in()";
      if (elem_cls & CL_PRIMITIVE) {
        string suffix;
        for (unsigned int i = 1; i < arr->n_dims(); ++i)
          suffix += use_cxx11 ? "->data()" : "[0]";
        be_global->impl_ <<
          "  return strm.write_" << getSerializerName(elem)
          << "_array(arr" << accessor << suffix << ", " << n_elems << ");\n";
      } else { // Enum, String, Struct, Array, Sequence, Union
        {
          string indent = "  ";
          NestedForLoops nfl("CORBA::ULong", "i", arr, indent);
          if (!use_cxx11 && (elem_cls & CL_ARRAY)) {
            be_global->impl_ <<
              indent << cxx_elem << "_var tmp_var = " << cxx_elem
              << "_dup(arr" << nfl.index_ << ");\n" <<
              indent << cxx_elem << "_forany tmp = tmp_var.inout();\n" <<
              streamAndCheck("<< tmp", indent.size());
          } else {
            string suffix = (elem_cls & CL_STRING) ? (use_cxx11 ? "" : ".in()") : "";
            string pre;
            if (use_cxx11 && (elem_cls & (CL_ARRAY | CL_SEQUENCE))) {
              pre = "IDL::DistinctType<const " + cxx_elem + ", " +
                dds_generator::scoped_helper(arr->base_type()->name(), "_") + "_tag>(";
              suffix += ')';
            }
            be_global->impl_ <<
              streamAndCheck("<< " + pre + "arr" + nfl.index_ + suffix , indent.size());
          }
        }
        be_global->impl_ << "  return true;\n";
      }
    }
    {
      Function extraction("operator>>", "bool");
      extraction.addArg("strm", "Serializer&");
      extraction.addArg(use_cxx11 ? "wrap" : "arr", cxx);
      extraction.endArgs();
      be_global->impl_ <<
        "  bool discard_flag = false;\n"
        "  const Encoding& encoding = strm.encoding();\n"
        "  ACE_UNUSED_ARG(encoding);\n";
      std::vector<string> code;
      code.push_back("if (!strm.read_delimiter(total_size)) {");
      code.push_back("  return false;");
      code.push_back("}");
      generate_dheader_code(code, !primitive);
      if (!primitive) {
        be_global->impl_ << "  const size_t end_of_seq = strm.pos() + total_size;\n";
      }

      be_global->impl_ << unwrap;
      const std::string accessor = use_cxx11 ? ".data()" : ".out()";
      if (elem_cls & CL_PRIMITIVE) {
        string suffix;
        for (unsigned int i = 1; i < arr->n_dims(); ++i)
          suffix += use_cxx11 ? "->data()" : "[0]";
        be_global->impl_ <<
          "  return strm.read_" << getSerializerName(elem)
          << "_array(arr" << accessor << suffix << ", " << n_elems << ");\n";
      } else { // Enum, String, Struct, Array, Sequence, Union
        string indent = "  ";
        string suffix = "";
        NestedForLoops nfl("CORBA::ULong", "i", arr, indent);

        if (!use_cxx11 && (elem_cls & CL_ARRAY)) {
          const string typedefname = scoped(arr->base_type()->name());
          be_global->impl_ <<
            indent << typedefname << "_var tmp = " << typedefname << "_alloc();\n" <<
            indent << typedefname << "_forany fa = tmp.inout();\n" <<
            indent <<  "    if (!(strm >> fa)) {\n";
        } else {
          string intro;
          be_global->impl_ <<
            "    if (!" <<
              streamCommon("", arr->base_type(), string(">> ") + "arr" + nfl.index_, intro) <<
              ") {\n";
        }
        if (try_construct == tryconstructfailaction_use_default) {
          if (elem_cls & CL_ARRAY) {
            be_global->impl_ << "      " << type_to_default(elem, "arr" + nfl.index_) <<
                                "      strm.set_construction_status(Serializer::ConstructionSuccessful);\n";
          } else {
            be_global->impl_ << "      " << type_to_default(elem, "arr" + nfl.index_ + suffix) <<
                                "      strm.set_construction_status(Serializer::ConstructionSuccessful);\n";
          }
        } else if ((try_construct == tryconstructfailaction_trim) && (elem_cls & CL_BOUNDED) &&
                   ((elem_cls & CL_STRING) || (elem_cls & CL_SEQUENCE))) {
          if (elem_cls & CL_STRING) {
            string check_not_empty = use_cxx11 ? "!arr" + nfl.index_ + ".empty()" : "arr" + nfl.index_ + ".in()";
            string get_length = use_cxx11 ? "arr" + nfl.index_ + ".length()" : "ACE_OS::strlen(arr" + nfl.index_ + ".in())";
            string inout = use_cxx11 ? "" : ".inout()";
            if (use_cxx11){
              be_global->impl_ <<
                "        if (strm.good_bit() && " << check_not_empty << " && ("
                        << bounded_arg(elem) << " < " << get_length << ")) {\n"
                "          arr" << nfl.index_ << inout << ".resize(" << bounded_arg(elem) <<  ");\n"
                "        }";
            } else {
              be_global->impl_ <<
                "        if (strm.good_bit() && " << check_not_empty << " && ("
                        << bounded_arg(elem) << " < " << get_length << ")) {\n"
                "          arr" << nfl.index_ << inout << "[" << bounded_arg(elem) << "] = 0;\n"
                "        }";
            }
            be_global->impl_ << " else {\n";
            skip_to_end_array();
            be_global->impl_ << "      }\n";
          } else if (elem_cls & CL_SEQUENCE) {
            be_global->impl_ << "      if(strm.get_construction_status() == Serializer::ElementConstructionFailure) {\n";
            skip_to_end_array();
            be_global->impl_ << "      } else {\n"
                                "        strm.set_construction_status(Serializer::ConstructionSuccessful);\n"
                                "      }\n";
          }
        } else {
          //discard/default
          skip_to_end_array();
        }
        if (!use_cxx11 && (elem_cls & CL_ARRAY)) {
          const string typedefname = scoped(arr->base_type()->name());
          be_global->impl_ <<
            indent << "} else {\n" <<
            indent << "  " << typedefname << "_copy(arr" << nfl.index_ << ", tmp.in());\n";
        }
        be_global->impl_ << "    }\n";
      }
      be_global->impl_ <<
        "  if (discard_flag) {\n"
        "    strm.set_construction_status(Serializer::ElementConstructionFailure);\n"
        "    return false;\n"
        "  }\n"
        "  return true;\n";
    }
  }

  void gen_anonymous_array(const FieldInfo& af)
  {
    const bool use_cxx11 = be_global->language_mapping() == BE_GlobalData::LANGMAP_CXX11;
    const bool primitive = af.as_cls_ & CL_PRIMITIVE;
    if (!af.as_act_->in_main_file() && af.as_act_->node_type() != AST_Decl::NT_pre_defined) {
      be_global->add_referenced(af.as_act_->file_name().c_str());
    }
    if (use_cxx11) {
      be_global->header_ << "struct " << af.underscored_ << "_tag {};\n\n";
    }
    {
      Function set_default("set_default", "void");
      set_default.addArg("stru", af.ref_);
      set_default.endArgs();
      AST_Type* elem = resolveActualType(af.arr_->base_type());
      string indent = "  ";
      string var_name = "stru";
      if (use_cxx11) {
        std::string n = scoped(af.arr_->name());
        n = n.substr(0, n.rfind("::") + 2) + "AnonymousType_" + af.arr_->local_name()->get_string();
        be_global->impl_ << "  " << n + "& arr = stru;\n";
        var_name = "arr";
      }
      NestedForLoops nfl("CORBA::ULong", "i", af.arr_, indent);
      be_global->impl_ << "    " << type_to_default(elem, var_name + nfl.index_);
    }
    {

      Function serialized_size("serialized_size", "void");
      serialized_size.addArg("encoding", "const Encoding&");
      serialized_size.addArg("size", "size_t&");
      serialized_size.addArg(af.arg_.c_str(), af.const_ref_);
      serialized_size.endArgs();

      std::vector<string> code;
      code.push_back("serialized_size_delimiter(encoding, size);");
      generate_dheader_code(code, !primitive, false);

      be_global->impl_ << af.const_unwrap_;
      if (af.as_cls_ & CL_ENUM) {
        be_global->impl_ <<
          "  OpenDDS::DCPS::primitive_serialized_size_ulong(encoding, size);\n";
        if (af.n_elems_ > 1) {
          be_global->impl_ <<
            "  OpenDDS::DCPS::primitive_serialized_size_ulong(encoding, size, "
              << (af.n_elems_ - 1) << ");\n";
        }
      } else if (af.as_cls_ & CL_PRIMITIVE) {
        std::ostringstream n_elems_ss;
        n_elems_ss << af.n_elems_;
        be_global->impl_ <<
          "  " << getSizeExprPrimitive(af.as_act_, n_elems_ss.str()) << ";\n";
      } else { // String, Struct, Array, Sequence, Union
        string indent = "  ";
        NestedForLoops nfl("CORBA::ULong", "i", af.arr_, indent);
        if (af.as_cls_ & CL_STRING) {
          be_global->impl_ <<
            indent << "OpenDDS::DCPS::primitive_serialized_size_ulong(encoding, size);\n" <<
            indent;
          if (use_cxx11) {
            be_global->impl_ << "size += arr" << nfl.index_ << ".size()";
          } else {
            be_global->impl_ << "size += ACE_OS::strlen(arr" << nfl.index_ << ".in())";
          }
          be_global->impl_ << ((af.as_cls_ & CL_WIDE)
            ? " * OpenDDS::DCPS::char16_cdr_size;\n"
            : " + 1;\n");
        } else if (!use_cxx11 && (af.as_cls_ & CL_ARRAY)) {
          be_global->impl_ <<
            indent << af.scoped_elem_ << "_var tmp_var = " << af.scoped_elem_
            << "_dup(arr" << nfl.index_ << ");\n" <<
            indent << af.scoped_elem_ << "_forany tmp = tmp_var.inout();\n" <<
            indent << "serialized_size(encoding, size, tmp);\n";
        } else { // Struct, Sequence, Union, C++11 Array
          string pre, post;
          if (use_cxx11 && (af.as_cls_ & (CL_ARRAY | CL_SEQUENCE))) {
            pre = "IDL::DistinctType<const " + af.scoped_elem_ + ", " +
              dds_generator::scoped_helper(af.as_base_->name(), "_") + "_tag>(";
            post = ')';
          }
          be_global->impl_ <<
            indent << "serialized_size(encoding, size, "
              << pre << "arr" << nfl.index_ << post << ");\n";
        }
      }
    }
    {
      Function insertion("operator<<", "bool");
      insertion.addArg("strm", "Serializer&");
      insertion.addArg(af.arg_.c_str(), af.const_ref_);
      insertion.endArgs();

      be_global->impl_ << "  const Encoding& encoding = strm.encoding();\n";
      std::vector<string> code;
      code.push_back(string("serialized_size(strm.encoding(), total_size, ") + (use_cxx11 ? "wrap" : "arr") + ");");
      code.push_back("if (!strm.write_delimiter(total_size)) {");
      code.push_back("  return false;");
      code.push_back("}");
      generate_dheader_code(code, !primitive);

      be_global->impl_ << af.const_unwrap_;
      const std::string accessor = use_cxx11 ? ".data()" : ".in()";
      if (af.as_cls_ & CL_PRIMITIVE) {
        string suffix;
        for (unsigned int i = 1; i < af.arr_->n_dims(); ++i)
          suffix += use_cxx11 ? "->data()" : "[0]";
        be_global->impl_ <<
          "  return strm.write_" << getSerializerName(af.as_act_)
          << "_array(arr" << accessor << suffix << ", " << af.n_elems_ << ");\n";
      } else { // Enum, String, Struct, Array, Sequence, Union
        {
          string indent = "  ";
          NestedForLoops nfl("CORBA::ULong", "i", af.arr_, indent);
          if (!use_cxx11 && (af.as_cls_ & CL_ARRAY)) {
            be_global->impl_ <<
              indent << af.scoped_elem_ << "_var tmp_var = " << af.scoped_elem_
              << "_dup(arr" << nfl.index_ << ");\n" <<
              indent << af.scoped_elem_ << "_forany tmp = tmp_var.inout();\n" <<
              streamAndCheck("<< tmp", indent.size());
          } else {
            string suffix = (af.as_cls_ & CL_STRING) ? (use_cxx11 ? "" : ".in()") : "";
            string pre;
            if (use_cxx11 && (af.as_cls_ & (CL_ARRAY | CL_SEQUENCE))) {
              pre = "IDL::DistinctType<const " + af.scoped_elem_ + ", " +
                dds_generator::scoped_helper(af.as_base_->name(), "_") + "_tag>(";
              suffix += ')';
            }
            be_global->impl_ <<
              streamAndCheck("<< " + pre + "arr" + nfl.index_ + suffix , indent.size());
          }
        }
        be_global->impl_ << "  return true;\n";
      }
    }
    {
      Function extraction("operator>>", "bool");
      extraction.addArg("strm", "Serializer&");
      extraction.addArg(af.arg_.c_str(), af.ref_);
      extraction.endArgs();

      AST_Annotation_Appl* ann_appl = af.arr_->base_type_annotations().find("::@try_construct");
      TryConstructFailAction try_construct = get_try_construct_annotation(ann_appl);

      be_global->impl_ << "  bool discard_flag = false;\n"
                       << "  const Encoding& encoding = strm.encoding();\n";
      std::vector<string> code;
      code.push_back("if (!strm.read_delimiter(total_size)) {");
      code.push_back("  return false;");
      code.push_back("}");
      generate_dheader_code(code, !primitive);
      if (!primitive) {
        be_global->impl_ << "  const size_t end_of_seq = strm.pos() + total_size;\n";
      }

      be_global->impl_ << af.unwrap_;
      const std::string accessor = use_cxx11 ? ".data()" : ".out()";
      if (af.as_cls_ & CL_PRIMITIVE) {
        string suffix;
        for (unsigned int i = 1; i < af.arr_->n_dims(); ++i)
          suffix += use_cxx11 ? "->data()" : "[0]";
        be_global->impl_ <<
          "  return strm.read_" << getSerializerName(af.as_act_)
          << "_array(arr" << accessor << suffix << ", " << af.n_elems_ << ");\n";
      } else { // Enum, String, Struct, Array, Sequence, Union
        string indent = "  ";
        string suffix = "";
        NestedForLoops nfl("CORBA::ULong", "i", af.arr_, indent);
        if (!use_cxx11 && (af.as_cls_ & CL_ARRAY)) {
          const string typedefname = scoped(af.arr_->base_type()->name());
          be_global->impl_ <<
            indent << typedefname << "_var tmp = " << typedefname
            << "_alloc();\n" <<
            indent << typedefname << "_forany fa = tmp.inout();\n"
            << indent <<  "if (!(strm >> fa)) {\n";
        } else {
          string intro;
          be_global->impl_ << "    if (!"
            << streamCommon("", af.as_base_, string(">> ") + "arr" + nfl.index_, intro)
            << ") {\n";
        }
        if (try_construct == tryconstructfailaction_use_default) {
          if (af.as_cls_ & CL_ARRAY) {
            be_global->impl_ <<
              "      " << type_to_default(af.as_base_, "arr" + nfl.index_) <<
              "      strm.set_construction_status(Serializer::ConstructionSuccessful);\n";
          } else {
            be_global->impl_ <<
              "      " << type_to_default(af.as_base_, "arr" + nfl.index_ + suffix) <<
              "      strm.set_construction_status(Serializer::ConstructionSuccessful);\n";
          }
        } else if ((try_construct == tryconstructfailaction_trim) && (af.as_cls_ & CL_BOUNDED) &&
                   ((af.as_cls_ & CL_STRING) || (af.as_cls_ & CL_SEQUENCE))) {
          if (af.as_cls_ & CL_STRING){
            string check_not_empty = use_cxx11 ? "!arr" + nfl.index_ + ".empty()" : "arr" + nfl.index_ + ".in()";
            string get_length = use_cxx11 ? "arr" + nfl.index_ + ".length()" : "ACE_OS::strlen(arr" + nfl.index_ + ".in())";
            string inout = use_cxx11 ? "" : ".inout()";
            if (use_cxx11){
              be_global->impl_ <<
                "        if (strm.good_bit() && " << check_not_empty << " && ("
                        << bounded_arg(af.as_act_) << " < " << get_length << ")) {\n"
                "        arr" << nfl.index_ << inout << ".resize(" << bounded_arg(af.as_act_) <<  ");\n"
                "        }";
            } else {
              be_global->impl_ <<
                "        if (strm.good_bit() && " << check_not_empty << " && ("
                        << bounded_arg(af.as_act_) << " < " << get_length << ")) {\n"
                "        arr" << nfl.index_ << inout << "[" << bounded_arg(af.as_act_) << "] = 0;\n"
                "        }";
            }
            be_global->impl_ << " else {\n";
            skip_to_end_array();
            be_global->impl_ << "      }\n";
          } else if (af.as_cls_ & CL_SEQUENCE) {
            be_global->impl_ << "      if(strm.get_construction_status() == Serializer::ElementConstructionFailure) {\n";
            skip_to_end_array();
            be_global->impl_ <<
              "      } else {\n"
              "        strm.set_construction_status(Serializer::ConstructionSuccessful);\n"
              "      }\n";
          }
        } else {
          //discard/default
          skip_to_end_array();
        }
        if (!use_cxx11 && (af.as_cls_ & CL_ARRAY)) {
          const string typedefname = scoped(af.arr_->base_type()->name());
          be_global->impl_ <<
            indent << "} else {\n" <<
            indent << "  " << typedefname << "_copy(arr" << nfl.index_ << ", tmp.in());\n";
        }
        be_global->impl_ << "    }\n";
      }
      be_global->impl_ <<
        "  if (discard_flag) {\n"
        "    strm.set_construction_status(Serializer::ElementConstructionFailure);\n"
        "    return false;\n"
        "  }\n"
        "  return true;\n";
    }
  }

  string getArrayForany(const char* prefix, const char* fname,
                        const string& cxx_fld, const string& temp_var_suffix = "")
  {
    string local = fname;
    if (local.size() > 2 && local.substr(local.size() - 2, 2) == "()") {
      local.erase(local.size() - 2);
    }
    return cxx_fld + "_forany " + prefix + '_' + local + temp_var_suffix + "(const_cast<"
      + cxx_fld + "_slice*>(" + prefix + "." + fname + "));";
  }

  // This function looks through the fields of a struct for the key
  // specified and returns the AST_Type associated with that key.
  // Because the key name can contain indexed arrays and nested
  // structures, things can get interesting.
  AST_Type* find_type(AST_Structure* struct_node, const string& key)
  {
    string key_base = key;   // the field we are looking for here
    string key_rem;          // the sub-field we will look for recursively
    bool is_array = false;
    size_t pos = key.find_first_of(".[");
    if (pos != string::npos) {
      key_base = key.substr(0, pos);
      if (key[pos] == '[') {
        is_array = true;
        size_t l_brack = key.find("]");
        if (l_brack == string::npos) {
          throw string("Missing right bracket");
        } else if (l_brack != key.length()) {
          key_rem = key.substr(l_brack+1);
        }
      } else {
        key_rem = key.substr(pos+1);
      }
    }

    const Fields fields(struct_node);
    const Fields::Iterator fields_end = fields.end();
    for (Fields::Iterator i = fields.begin(); i != fields_end; ++i) {
      AST_Field* field = *i;
      if (key_base == field->local_name()->get_string()) {
        AST_Type* field_type = resolveActualType(field->field_type());
        if (!is_array && key_rem.empty()) {
          // The requested key field matches this one.  We do not allow
          // arrays (must be indexed specifically) or structs (must
          // identify specific sub-fields).
          AST_Structure* sub_struct = dynamic_cast<AST_Structure*>(field_type);
          if (sub_struct != 0) {
            throw string("Structs not allowed as keys");
          }
          AST_Array* array_node = dynamic_cast<AST_Array*>(field_type);
          if (array_node != 0) {
            throw string("Arrays not allowed as keys");
          }
          return field_type;
        } else if (is_array) {
          // must be a typedef of an array
          AST_Array* array_node = dynamic_cast<AST_Array*>(field_type);
          if (array_node == 0) {
            throw string("Indexing for non-array type");
          }
          if (array_node->n_dims() > 1) {
            throw string("Only single dimension arrays allowed in keys");
          }
          if (key_rem == "") {
            return array_node->base_type();
          } else {
            // This must be a struct...
            if ((key_rem[0] != '.') || (key_rem.length() == 1)) {
              throw string("Unexpected characters after array index");
            } else {
              // Set up key_rem and field_type and let things fall into
              // the struct code below
              key_rem = key_rem.substr(1);
              field_type = array_node->base_type();
            }
          }
        }

        // nested structures
        AST_Structure* sub_struct = dynamic_cast<AST_Structure*>(field_type);
        if (sub_struct == 0) {
          throw string("Expected structure field for ") + key_base;
        }

        // find type of nested struct field
        return find_type(sub_struct, key_rem);
      }
    }
    throw string("Field not found.");
  }

  bool is_bounded_type(AST_Type* type, Encoding encoding)
  {
    bool bounded = true;
    static std::vector<AST_Type*> type_stack;
    type = resolveActualType(type);
    for (unsigned int i = 0; i < type_stack.size(); i++) {
      // If we encounter the same type recursively, then we are unbounded
      if (type == type_stack[i]) return false;
    }
    type_stack.push_back(type);
    Classification fld_cls = classify(type);
    if ((fld_cls & CL_STRING) && !(fld_cls & CL_BOUNDED)) {
      bounded = false;
    } else if (fld_cls & CL_STRUCTURE) {
      const ExtensibilityKind exten = be_global->extensibility(type);
      if (exten != extensibilitykind_final && encoding != encoding_unaligned_cdr) {
        /**
         * This is a workaround for not properly implementing
         * max_serialized_size for XCDR.
         */
        bounded = false;
      } else {
        const Fields fields(dynamic_cast<AST_Structure*>(type));
        const Fields::Iterator fields_end = fields.end();
        for (Fields::Iterator i = fields.begin(); i != fields_end; ++i) {
          if (!is_bounded_type((*i)->field_type(), encoding)) {
            bounded = false;
            break;
          }
        }
      }
    } else if (fld_cls & CL_SEQUENCE) {
      if (fld_cls & CL_BOUNDED) {
        AST_Sequence* seq_node = dynamic_cast<AST_Sequence*>(type);
        if (!is_bounded_type(seq_node->base_type(), encoding)) bounded = false;
      } else {
        bounded = false;
      }
    } else if (fld_cls & CL_ARRAY) {
      AST_Array* array_node = dynamic_cast<AST_Array*>(type);
      if (!is_bounded_type(array_node->base_type(), encoding)) bounded = false;
    } else if (fld_cls & CL_UNION) {
      const ExtensibilityKind exten = be_global->extensibility(type);
      if (exten != extensibilitykind_final && encoding != encoding_unaligned_cdr) {
        /**
         * This is a workaround for not properly implementing
         * max_serialized_size for XCDR.
         */
        bounded = false;
      } else {
        const Fields fields(dynamic_cast<AST_Union*>(type));
        const Fields::Iterator fields_end = fields.end();
        for (Fields::Iterator i = fields.begin(); i != fields_end; ++i) {
          if (!is_bounded_type((*i)->field_type(), encoding)) {
            bounded = false;
            break;
          }
        }
      }
    }
    type_stack.pop_back();
    return bounded;
  }

  /**
   * Convert a compiler Encoding value to the string name of the corresponding
   * OpenDDS::DCPS::Encoding::XcdrVersion.
   */
  std::string encoding_to_xcdr_version(Encoding encoding)
  {
    switch (encoding) {
    case encoding_xcdr1:
      return "Encoding::XCDR_VERSION_1";
    case encoding_xcdr2:
      return "Encoding::XCDR_VERSION_2";
    default:
      return "Encoding::XCDR_VERSION_NONE";
    }
  }

  /**
   * Convert a compiler Encoding value to the string name of the corresponding
   * OpenDDS::DCPS::Encoding::Kind.
   */
  std::string encoding_to_encoding_kind(Encoding encoding)
  {
    switch (encoding) {
    case encoding_xcdr1:
      return "Encoding::KIND_XCDR1";
    case encoding_xcdr2:
      return "Encoding::KIND_XCDR2";
    default:
      return "Encoding::KIND_UNALIGNED_CDR";
    }
  }

  size_t max_alignment(Encoding encoding)
  {
    switch (encoding) {
    case encoding_xcdr1:
      return 8;
    case encoding_xcdr2:
      return 4;
    default:
      return 0;
    }
  }

  void align(Encoding encoding, size_t& value, size_t by)
  {
    const size_t align_by = std::min(max_alignment(encoding), by);
    if (align_by) {
      const size_t offset_by = value % align_by;
      if (offset_by) {
        value += align_by - offset_by;
      }
    }
  }

  void idl_max_serialized_size_dheader(
    Encoding encoding, ExtensibilityKind exten, size_t& size)
  {
    if (exten != extensibilitykind_final && encoding == encoding_xcdr2) {
      align(encoding, size, 4);
      size += 4;
    }
  }

  void idl_max_serialized_size(Encoding encoding, size_t& size, AST_Type* type);

  // Max marshaled size of repeating 'type' 'n' times in the stream
  // (for an array or sequence)
  void idl_max_serialized_size_repeating(
    Encoding encoding, size_t& size, AST_Type* type, size_t n)
  {
    if (n > 0) {
      // 1st element may need padding relative to whatever came before
      idl_max_serialized_size(encoding, size, type);
    }
    if (n > 1) {
      // subsequent elements may need padding relative to prior element
      // TODO(iguessthislldo): https://github.com/objectcomputing/OpenDDS/pull/1668#discussion_r432521888
      const size_t prev_size = size;
      idl_max_serialized_size(encoding, size, type);
      size += (n - 2) * (size - prev_size);
    }
  }

  /// Should only be called on bounded types
  void idl_max_serialized_size(Encoding encoding, size_t& size, AST_Type* type)
  {
    type = resolveActualType(type);
    const ExtensibilityKind exten = be_global->extensibility(type);
    switch (type->node_type()) {
    case AST_Decl::NT_pre_defined: {
      AST_PredefinedType* p = dynamic_cast<AST_PredefinedType*>(type);
      switch (p->pt()) {
      case AST_PredefinedType::PT_char:
      case AST_PredefinedType::PT_boolean:
      case AST_PredefinedType::PT_octet:
        size += 1;
        break;
      case AST_PredefinedType::PT_short:
      case AST_PredefinedType::PT_ushort:
        align(encoding, size, 2);
        size += 2;
        break;
      case AST_PredefinedType::PT_wchar:
        align(encoding, size, 2);
        size += 2;
        break;
      case AST_PredefinedType::PT_long:
      case AST_PredefinedType::PT_ulong:
      case AST_PredefinedType::PT_float:
        align(encoding, size, 4);
        size += 4;
        break;
      case AST_PredefinedType::PT_longlong:
      case AST_PredefinedType::PT_ulonglong:
      case AST_PredefinedType::PT_double:
        align(encoding, size, 8);
        size += 8;
        break;
      case AST_PredefinedType::PT_longdouble:
        align(encoding, size, 16);
        size += 16;
        break;
      default:
        // Anything else shouldn't be in a DDS type or is unbounded.
        break;
      }
      break;
    }
    case AST_Decl::NT_enum:
      align(encoding, size, 4);
      size += 4;
      break;
    case AST_Decl::NT_string:
    case AST_Decl::NT_wstring: {
      AST_String* string_node = dynamic_cast<AST_String*>(type);
      align(encoding, size, 4);
      size += 4;
      const int width = (string_node->width() == 1) ? 1 : 2 /*UTF-16*/;
      size += width * string_node->max_size()->ev()->u.ulval;
      if (type->node_type() == AST_Decl::NT_string) {
        size += 1; // narrow string includes the null terminator
      }
      break;
    }
    case AST_Decl::NT_struct: {
      const Fields fields(dynamic_cast<AST_Structure*>(type));
      const Fields::Iterator fields_end = fields.end();
      idl_max_serialized_size_dheader(encoding, exten, size);
      // TODO(iguessthislldo) Handle Parameter List
      for (Fields::Iterator i = fields.begin(); i != fields_end; ++i) {
        idl_max_serialized_size(encoding, size, (*i)->field_type());
      }
      break;
    }
    case AST_Decl::NT_sequence: {
      AST_Sequence* seq_node = dynamic_cast<AST_Sequence*>(type);
      AST_Type* base_node = seq_node->base_type();
      idl_max_serialized_size_dheader(encoding, exten, size);
      size_t bound = seq_node->max_size()->ev()->u.ulval;
      align(encoding, size, 4);
      size += 4;
      idl_max_serialized_size_repeating(encoding, size, base_node, bound);
      break;
    }
    case AST_Decl::NT_array: {
      AST_Array* array_node = dynamic_cast<AST_Array*>(type);
      AST_Type* base_node = array_node->base_type();
      idl_max_serialized_size_dheader(encoding, exten, size);
      size_t array_size = 1;
      AST_Expression** dims = array_node->dims();
      for (unsigned long i = 0; i < array_node->n_dims(); i++) {
        array_size *= dims[i]->ev()->u.ulval;
      }
      idl_max_serialized_size_repeating(encoding, size, base_node, array_size);
      break;
    }
    case AST_Decl::NT_union: {
      AST_Union* union_node = dynamic_cast<AST_Union*>(type);
      idl_max_serialized_size_dheader(encoding, exten, size);
      // TODO(iguessthislldo) Handle Parameter List
      idl_max_serialized_size(encoding, size, union_node->disc_type());
      size_t largest_field_size = 0;
      const size_t starting_size = size;
      const Fields fields(union_node);
      const Fields::Iterator fields_end = fields.end();
      for (Fields::Iterator i = fields.begin(); i != fields_end; ++i) {
        idl_max_serialized_size(encoding, size, (*i)->field_type());
        size_t field_size = size - starting_size;
        if (field_size > largest_field_size) {
          largest_field_size = field_size;
        }
        // rewind:
        size = starting_size;
      }
      size += largest_field_size;
      break;
    }
    default:
      // Anything else should be not here or is unbounded
      break;
    }
  }
}

bool marshal_generator::gen_typedef(AST_Typedef*, UTL_ScopedName* name, AST_Type* base, const char*)
{
  switch (base->node_type()) {
  case AST_Decl::NT_sequence:
    gen_sequence(name, dynamic_cast<AST_Sequence*>(base));
    break;
  case AST_Decl::NT_array:
    gen_array(name, dynamic_cast<AST_Array*>(base));
    break;
  default:
    return true;
  }
  return true;
}

namespace {
  // common to both fields (in structs) and branches (in unions)
  string findSizeCommon(const string& name, AST_Type* type,
                        const string& prefix, string& intro,
                        const string& = "", bool = false) // same sig as streamCommon
  {
    const bool use_cxx11 = be_global->language_mapping() == BE_GlobalData::LANGMAP_CXX11;
    const bool is_union_member = prefix == "uni";

    AST_Type* typedeff = type;
    type = resolveActualType(type);
    Classification fld_cls = classify(type);

    const string qual = prefix + '.' + insert_cxx11_accessor_parens(name, is_union_member);
    const string indent = (is_union_member) ? "    " : "  ";

    if (fld_cls & CL_ENUM) {
      return indent + "OpenDDS::DCPS::primitive_serialized_size_ulong(encoding, size);\n";
    } else if (fld_cls & CL_STRING) {
      const string suffix = is_union_member ? "" : ".in()";
      const string get_size = use_cxx11 ? (qual + ".size()")
        : ("ACE_OS::strlen(" + qual + suffix + ")");
      return indent + "OpenDDS::DCPS::primitive_serialized_size_ulong(encoding, size);\n" +
        indent + "size += " + get_size
        + ((fld_cls & CL_WIDE) ? " * OpenDDS::DCPS::char16_cdr_size;\n"
                               : " + 1;\n");
    } else if (fld_cls & CL_PRIMITIVE) {
      AST_PredefinedType* p = dynamic_cast<AST_PredefinedType*>(type);
      if (p->pt() == AST_PredefinedType::PT_longdouble) {
        // special case use to ACE's NONNATIVE_LONGDOUBLE in CDR_Base.h
        return indent +
          "primitive_serialized_size(encoding, size, ACE_CDR::LongDouble());\n";
      }
      return indent + "primitive_serialized_size(encoding, size, " +
        getWrapper(qual, type, WD_OUTPUT) + ");\n";
    } else if (fld_cls == CL_UNKNOWN) {
      return ""; // warning will be issued for the serialize functions
    } else { // sequence, struct, union, array
      string fieldref = prefix, local = insert_cxx11_accessor_parens(name, is_union_member);
      string tdname = scoped(typedeff->name());
      if (!use_cxx11 && (fld_cls & CL_ARRAY)) {
        intro += "  " + getArrayForany(prefix.c_str(), name.c_str(), tdname) + '\n';
        fieldref += '_';
        if (local.size() > 2 && local.substr(local.size() - 2) == "()") {
          local.erase(local.size() - 2);
        }
      } else if (use_cxx11 && (fld_cls & (CL_SEQUENCE | CL_ARRAY))) {
        fieldref = "IDL::DistinctType<const " + tdname + ", " +
          dds_generator::scoped_helper(typedeff->name(), "_") + "_tag>("
          + fieldref + '.';
        local += ')';
      } else {
        fieldref += '.';
      }
      return indent +
        "serialized_size(encoding, size, " + fieldref + local + ");\n";
    }
  }

  bool findSizeAnonymous(AST_Field* field, const string& prefix, string& intro, string& expr)
  {
    FieldInfo af(*field);
    if (!af.anonymous()) {
      return false;
    }
    string fieldref = prefix, local = insert_cxx11_accessor_parens(af.name_, false);
    if (af.cxx11()) {
      fieldref = af.const_ref_ + "(" + fieldref + '.';
      local += ')';
    } else if (af.cls_ & CL_ARRAY) {
      intro += "  " + getArrayForany(prefix.c_str(), af.name_.c_str(), af.scoped_type_) + '\n';
      fieldref += '_';
      if (local.size() > 2 && local.substr(local.size() - 2) == "()") {
        local.erase(local.size() - 2);
      }
    } else {
      fieldref += '.';
    }
    expr += "  serialized_size(encoding, size, " + fieldref + local + ");\n";
    return true;
  }

  // common to both fields (in structs) and branches (in unions)
  string streamCommon(const string& name, AST_Type* type,
                      const string& prefix, string& intro,
                      const string& stru, bool printing)
  {
    const bool use_cxx11 = be_global->language_mapping() == BE_GlobalData::LANGMAP_CXX11;
    const bool is_union_member = prefix.substr(3) == "uni";

    AST_Type* typedeff = type;
    const string tdname = scoped(typedeff->name());
    type = resolveActualType(type);
    Classification fld_cls = classify(type);

    string qual = prefix + '.' + insert_cxx11_accessor_parens(name, is_union_member);
    // if there is a stray '.' on the end, strip it off
    if (qual[qual.length() - 1] == '.') qual.pop_back();
    const string shift = prefix.substr(0, 2),
                 expr = qual.substr(3);

    WrapDirection dir = (shift == ">>") ? WD_INPUT : WD_OUTPUT;
    if ((fld_cls & CL_STRING) && (dir == WD_INPUT)) {
      if ((fld_cls & CL_BOUNDED) && !printing) {
        const string args = expr + (use_cxx11 ? ", " : ".out(), ") + bounded_arg(type);
        return "(strm " + shift + ' ' + getWrapper(args, type, WD_INPUT) + ')';
      }
      return "(strm " + qual + (use_cxx11 ? "" : ".out()") + ')';
    } else if (fld_cls & CL_PRIMITIVE) {
      return "(strm " + shift + ' ' + getWrapper(expr, type, dir) + ')';
    } else if (fld_cls == CL_UNKNOWN) {
      if (dir == WD_INPUT) { // no need to warn twice
        std::cerr << "WARNING: field " << name << " can not be serialized.  "
          "The struct or union it belongs to (" << stru <<
          ") can not be used in an OpenDDS topic type." << std::endl;
      }
      return "false";
    } else { // sequence, struct, union, array, enum, string(insertion)
      string fieldref = prefix, local = insert_cxx11_accessor_parens(name, is_union_member);
      const bool accessor = local.size() > 2 && local.substr(local.size() - 2) == "()";
      if (!use_cxx11 && (fld_cls & CL_ARRAY)) {
        string pre = prefix;
        if (shift == ">>" || shift == "<<") {
          pre.erase(0, 3);
        }
        if (accessor) {
          local.erase(local.size() - 2);
        }
        //TODO: Was this redundant call fixed
        intro += "  " + getArrayForany(pre.c_str(), name.c_str(), tdname, "_sc") + '\n';
        local += "_sc";
        fieldref += '_';
      } else if (!local.empty()) {
        fieldref += '.';
      }

      if (fld_cls & CL_STRING) {
        if (!accessor && !use_cxx11) {
          local += ".in()";
        }
        if ((fld_cls & CL_BOUNDED) && !printing) {
          const string args = (fieldref + local).substr(3) + ", " + bounded_arg(type);
          return "(strm " + shift + ' ' + getWrapper(args, type, WD_OUTPUT) + ')';
        }
      } else if (use_cxx11 && (fld_cls & (CL_ARRAY | CL_SEQUENCE))) {
        return "(strm " + shift + " IDL::DistinctType<" +
          (dir == WD_OUTPUT ? "const " : "") + tdname + ", " +
          dds_generator::scoped_helper(typedeff->name(), "_") + "_tag>("
          + (fieldref + local).substr(3) + "))";
      }
      return "(strm " + fieldref + local + ')';
    }
  }

  bool streamAnonymous(AST_Field* field, const string& shift, string& intro, string& expr)
  {
    FieldInfo af(*field);
    if (!af.anonymous()) {
      return false;
    }
    const string stru = "stru";
    string local = insert_cxx11_accessor_parens(af.name_, false);
    const bool accessor = local.size() > 2 && local.substr(local.size() - 2) == "()";
    if (af.cxx11()) {
      expr += "(strm " + shift + " " + ((shift == "<<") ? af.const_ref_ : af.ref_)
        + "(" + stru + "." + local + "))";
    } else {
      const string fieldref = shift + ' ' + stru + ((af.cls_ & CL_ARRAY) ? '_' : '.');
      if (af.cls_ & CL_ARRAY) {
        if (accessor) {
          local.erase(local.size() - 2);
        }
        intro += "  " + getArrayForany(stru.c_str(), af.name_.c_str(), af.scoped_type_, "_sc") + '\n';
        local += "_sc";
      }
      expr += "(strm " + fieldref + local + ')';
    }
    return true;
  }

  bool isBinaryProperty_t(const string& cxx)
  {
    return cxx == "DDS::BinaryProperty_t";
  }

  bool genBinaryProperty_t(const string& cxx)
  {
    {
      Function serialized_size("serialized_size", "void");
      serialized_size.addArg("encoding", "const Encoding&");
      serialized_size.addArg("size", "size_t&");
      serialized_size.addArg("stru", "const " + cxx + "&");
      serialized_size.endArgs();
      be_global->impl_ <<
        "  if (stru.propagate) {\n"
        "    OpenDDS::DCPS::primitive_serialized_size_ulong(encoding, size);\n"
        "    size += ACE_OS::strlen(stru.name.in()) + 1;\n"
        "    serialized_size(encoding, size, stru.value);\n"
        "  }\n";
    }
    {
      Function insertion("operator<<", "bool");
      insertion.addArg("strm", "Serializer&");
      insertion.addArg("stru", "const " + cxx + "&");
      insertion.endArgs();
      be_global->impl_ <<
        "  if (stru.propagate) {\n"
        "    return (strm << stru.name.in()) && (strm << stru.value);\n"
        "  }\n"
        "  return true;\n";
    }
    {
      Function extraction("operator>>", "bool");
      extraction.addArg("strm", "Serializer&");
      extraction.addArg("stru", cxx + "&");
      extraction.endArgs();
      be_global->impl_ <<
        "  stru.propagate = true;\n"
        "  return (strm >> stru.name.out()) && (strm >> stru.value);\n";
    }
    return true;
  }

  bool isProperty_t(const string& cxx)
  {
    return cxx == "DDS::Property_t";
  }

  bool genProperty_t(const string& cxx)
  {
    {
      Function serialized_size("serialized_size", "void");
      serialized_size.addArg("encoding", "const Encoding&");
      serialized_size.addArg("size", "size_t&");
      serialized_size.addArg("stru", "const " + cxx + "&");
      serialized_size.endArgs();
      be_global->impl_ <<
        "  if (stru.propagate) {\n"
        "    OpenDDS::DCPS::primitive_serialized_size_ulong(encoding, size);\n"
        "    size += ACE_OS::strlen(stru.name.in()) + 1;\n"
        "    OpenDDS::DCPS::primitive_serialized_size_ulong(encoding, size);\n"
        "    size += ACE_OS::strlen(stru.value.in()) + 1;\n"
        "  }\n";
    }
    {
      Function insertion("operator<<", "bool");
      insertion.addArg("strm", "Serializer&");
      insertion.addArg("stru", "const " + cxx + "&");
      insertion.endArgs();
      be_global->impl_ <<
        "  if (stru.propagate) {\n"
        "    return (strm << stru.name.in()) && (strm << stru.value.in());\n"
        "  }\n"
        "  return true;\n";
    }
    {
      Function extraction("operator>>", "bool");
      extraction.addArg("strm", "Serializer&");
      extraction.addArg("stru", cxx + "&");
      extraction.endArgs();
      be_global->impl_ <<
        "  stru.propagate = true;\n"
        "  return (strm >> stru.name.out()) && (strm >> stru.value.out());\n";
    }
    return true;
  }

  bool isPropertyQosPolicy(const string& cxx)
  {
    return cxx == "DDS::PropertyQosPolicy";
  }

  bool genPropertyQosPolicy(const string& cxx)
  {
    {
      Function serialized_size("serialized_size", "void");
      serialized_size.addArg("encoding", "const Encoding&");
      serialized_size.addArg("size", "size_t&");
      serialized_size.addArg("stru", "const " + cxx + "&");
      serialized_size.endArgs();
      be_global->impl_ <<
        "  serialized_size(encoding, size, stru.value);\n"
        "  serialized_size(encoding, size, stru.binary_value);\n";
    }
    {
      Function insertion("operator<<", "bool");
      insertion.addArg("strm", "Serializer&");
      insertion.addArg("stru", "const " + cxx + "&");
      insertion.endArgs();
      be_global->impl_ <<
        "  return (strm << stru.value)\n"
        "    && (strm << stru.binary_value);\n";
    }
    {
      Function extraction("operator>>", "bool");
      extraction.addArg("strm", "Serializer&");
      extraction.addArg("stru", cxx + "&");
      extraction.endArgs();
      be_global->impl_ <<
        "  if (!(strm >> stru.value)) {\n"
        "    return false;\n"
        "  }\n"
        "  if (!strm.length() || !strm.skip(0, 4) || !strm.length()) {\n"
        "    return true; // optional member missing\n"
        "  }\n"
        "  return strm >> stru.binary_value;\n";
    }
    return true;
  }

  bool isSecuritySubmessage(const string& cxx)
  {
    return cxx == "OpenDDS::RTPS::SecuritySubmessage";
  }

  bool genSecuritySubmessage(const string& cxx)
  {
    {
      Function serialized_size("serialized_size", "void");
      serialized_size.addArg("encoding", "const Encoding&");
      serialized_size.addArg("size", "size_t&");
      serialized_size.addArg("stru", "const " + cxx + "&");
      serialized_size.endArgs();
      be_global->impl_ <<
        "  serialized_size(encoding, size, stru.smHeader);\n"
        "  primitive_serialized_size_octet(encoding, size, stru.content.length());\n";
    }
    {
      Function insertion("operator<<", "bool");
      insertion.addArg("strm", "Serializer&");
      insertion.addArg("stru", "const " + cxx + "&");
      insertion.endArgs();
      be_global->impl_ <<
        "  return (strm << stru.smHeader)\n"
        "    && strm.write_octet_array(stru.content.get_buffer(), "
        "stru.content.length());\n";
    }
    {
      Function extraction("operator>>", "bool");
      extraction.addArg("strm", "Serializer&");
      extraction.addArg("stru", cxx + "&");
      extraction.endArgs();
      be_global->impl_ <<
        "  if (strm >> stru.smHeader) {\n"
        "    stru.content.length(stru.smHeader.submessageLength);\n"
        "    if (strm.read_octet_array(stru.content.get_buffer(),\n"
        "                              stru.smHeader.submessageLength)) {\n"
        "      return true;\n"
        "    }\n"
        "  }\n"
        "  return false;\n";
    }
    return true;
  }

  bool isRtpsSpecialStruct(const string& cxx)
  {
    return cxx == "OpenDDS::RTPS::SequenceNumberSet"
      || cxx == "OpenDDS::RTPS::FragmentNumberSet";
  }

  bool genRtpsSpecialStruct(const string& cxx)
  {
    {
      Function serialized_size("serialized_size", "void");
      serialized_size.addArg("encoding", "const Encoding&");
      serialized_size.addArg("size", "size_t&");
      serialized_size.addArg("stru", "const " + cxx + "&");
      serialized_size.endArgs();
      be_global->impl_ <<
        "  size += "
        << ((cxx == "OpenDDS::RTPS::SequenceNumberSet") ? "12" : "8")
        << " + 4 * ((stru.numBits + 31) / 32); // RTPS Custom\n";
    }
    {
      Function insertion("operator<<", "bool");
      insertion.addArg("strm", "Serializer&");
      insertion.addArg("stru", "const " + cxx + "&");
      insertion.endArgs();
      be_global->impl_ <<
        "  if ((strm << stru.bitmapBase) && (strm << stru.numBits)) {\n"
        "    const CORBA::ULong M = (stru.numBits + 31) / 32;\n"
        "    if (stru.bitmap.length() < M) {\n"
        "      return false;\n"
        "    }\n"
        "    for (CORBA::ULong i = 0; i < M; ++i) {\n"
        "      if (!(strm << stru.bitmap[i])) {\n"
        "        return false;\n"
        "      }\n"
        "    }\n"
        "    return true;\n"
        "  }\n"
        "  return false;\n";
    }
    {
      Function extraction("operator>>", "bool");
      extraction.addArg("strm", "Serializer&");
      extraction.addArg("stru", cxx + "&");
      extraction.endArgs();
      be_global->impl_ <<
        "  if ((strm >> stru.bitmapBase) && (strm >> stru.numBits)) {\n"
        "    const CORBA::ULong M = (stru.numBits + 31) / 32;\n"
        "    if (M > 8) {\n"
        "      return false;\n"
        "    }\n"
        "    stru.bitmap.length(M);\n"
        "    for (CORBA::ULong i = 0; i < M; ++i) {\n"
        "      if (!(strm >> stru.bitmap[i])) {\n"
        "        return false;\n"
        "      }\n"
        "    }\n"
        "    return true;\n"
        "  }\n"
        "  return false;\n";
    }
    return true;
  }

  struct RtpsFieldCustomizer {

    explicit RtpsFieldCustomizer(const string& cxx)
    {
      if (cxx == "OpenDDS::RTPS::DataSubmessage") {
        cst_["inlineQos"] = "stru.smHeader.flags & 2";
        iQosOffset_ = "16";

      } else if (cxx == "OpenDDS::RTPS::DataFragSubmessage") {
        cst_["inlineQos"] = "stru.smHeader.flags & 2";
        iQosOffset_ = "28";

      } else if (cxx == "OpenDDS::RTPS::InfoReplySubmessage") {
        cst_["multicastLocatorList"] = "stru.smHeader.flags & 2";

      } else if (cxx == "OpenDDS::RTPS::InfoTimestampSubmessage") {
        cst_["timestamp"] = "!(stru.smHeader.flags & 2)";

      } else if (cxx == "OpenDDS::RTPS::InfoReplyIp4Submessage") {
        cst_["multicastLocator"] = "stru.smHeader.flags & 2";

      } else if (cxx == "OpenDDS::RTPS::SubmessageHeader") {
        preamble_ =
          "  strm.swap_bytes(ACE_CDR_BYTE_ORDER != (stru.flags & 1));\n";
      }
    }

    string getConditional(const string& field_name) const
    {
      if (cst_.empty()) {
        return "";
      }
      std::map<string, string>::const_iterator it = cst_.find(field_name);
      if (it != cst_.end()) {
        return it->second;
      }
      return "";
    }

    string preFieldRead(const string& field_name) const
    {
      if (cst_.empty() || field_name != "inlineQos" || iQosOffset_.empty()) {
        return "";
      }
      return "strm.skip(stru.octetsToInlineQos - " + iQosOffset_ + ")\n"
        "    && ";
    }

    std::map<string, string> cst_;
    string iQosOffset_, preamble_;
  };

  typedef void (*KeyIterationFn)(
    Encoding encoding,
    const string& key_name, AST_Type* ast_type,
    size_t* size,
    string* expr, string* intro);

  bool
  iterate_over_keys(
    Encoding encoding,
    AST_Structure* node,
    const std::string& struct_name,
    IDL_GlobalData::DCPS_Data_Type_Info* info,
    TopicKeys& keys,
    KeyIterationFn fn,
    size_t* size,
    string* expr, string* intro)
  {
    if (!info) {
      const TopicKeys::Iterator finished = keys.end();
      for (TopicKeys::Iterator i = keys.begin(); i != finished; ++i) {
        string key_access = i.path();
        AST_Type* straight_ast_type = i.get_ast_type();
        AST_Type* ast_type;
        if (i.root_type() == TopicKeys::UnionType) {
          AST_Union* union_type = dynamic_cast<AST_Union*>(straight_ast_type);
          if (!union_type) {
            std::cerr << "ERROR: Invalid key iterator for: " << struct_name;
            return false;
          }
          ast_type = dynamic_cast<AST_Type*>(union_type->disc_type());
          key_access.append("._d()");
        } else {
          ast_type = straight_ast_type;
        }
        fn(encoding, key_access, ast_type, size, expr, intro);
      }
    } else {
      IDL_GlobalData::DCPS_Data_Type_Info_Iter iter(info->key_list_);
      for (ACE_TString* kp = 0; iter.next(kp) != 0; iter.advance()) {
        const string key_name = ACE_TEXT_ALWAYS_CHAR(kp->c_str());
        AST_Type* field_type = 0;
        try {
          field_type = find_type(node, key_name);
        } catch (const string& error) {
          std::cerr << "ERROR: Invalid key specification for " << struct_name
                    << " (" << key_name << "). " << error << std::endl;
          return false;
        }
        fn(encoding, key_name, field_type, size, expr, intro);
      }
    }
    return true;
  }

  // Args must match KeyIterationFn.
  void idl_max_serialized_size_iteration(
    Encoding encoding, const string&, AST_Type* ast_type,
    size_t* size, string*, string*)
  {
    idl_max_serialized_size(encoding, *size, ast_type);
  }

  void serialized_size_iteration(
    Encoding, const string& key_name, AST_Type* ast_type,
    size_t*, string* expr, string* intro)
  {
    *expr += findSizeCommon(key_name, ast_type, "stru.t", *intro);
  }

  std::string fill_datareprseq(
    const OpenDDS::DataRepresentation& repr,
    const std::string& name,
    const std::string& indent)
  {
    std::vector<std::string> values;
    if (repr.xcdr1) {
      values.push_back("DDS::XCDR_DATA_REPRESENTATION");
    }
    if (repr.xcdr2) {
      values.push_back("DDS::XCDR2_DATA_REPRESENTATION");
    }
    if (repr.xml) {
      values.push_back("DDS::XML_DATA_REPRESENTATION");
    }
    if (repr.unaligned_cdr) {
      values.push_back("UNALIGNED_CDR_DATA_REPRESENTATION");
    }

    std::ostringstream ss;
    ss << indent << name << ".length(" << values.size() << ");\n";
    for (size_t i = 0; i < values.size(); ++i) {
      ss << indent << name << "[" << i << "] = " << values[i] << ";\n";
    }
    return ss.str();
  }

  bool is_bounded_topic_struct(AST_Type* type, Encoding encoding, bool key_only,
    TopicKeys& keys, IDL_GlobalData::DCPS_Data_Type_Info* info = 0)
  {
    bool bounded = true;
    if (key_only) {
      if (info) {
        IDL_GlobalData::DCPS_Data_Type_Info_Iter iter(info->key_list_);
        AST_Structure* const struct_type = dynamic_cast<AST_Structure*>(type);
        for (ACE_TString* kp = 0; iter.next(kp) != 0; iter.advance()) {
          const string key_name = ACE_TEXT_ALWAYS_CHAR(kp->c_str());
          AST_Type* field_type = find_type(struct_type, key_name);
          if (!is_bounded_type(field_type, encoding)) {
            bounded = false;
            break;
          }
        }
      } else {
        const TopicKeys::Iterator finished = keys.end();
        for (TopicKeys::Iterator i = keys.begin(); i != finished; ++i) {
          if (!is_bounded_type(i.get_ast_type(), encoding)) {
            bounded = false;
            break;
          }
        }
      }
    } else {
      bounded = is_bounded_type(type, encoding);
    }
    return bounded;
  }

  bool generate_marshal_traits_struct_bounds_functions(AST_Structure* node,
    TopicKeys& keys, IDL_GlobalData::DCPS_Data_Type_Info* info, bool key_only)
  {
    const char* function_prefix = key_only ? "key_only_" : "";
    AST_Type* const type_node = dynamic_cast<AST_Type*>(node);
    const Fields fields(node);
    const Fields::Iterator fields_end = fields.end();
    const std::string name = scoped(node->name());

    be_global->header_ <<
      "  static SerializedSizeBound " << function_prefix <<
        "serialized_size_bound(const Encoding& encoding)\n"
      "  {\n"
      "    switch (encoding.kind()) {\n";
    for (unsigned e = 0; e < encoding_count; ++e) {
      const Encoding encoding = static_cast<Encoding>(e);
      be_global->header_ <<
        "    case " << encoding_to_encoding_kind(encoding) << ":\n"
        "      return SerializedSizeBound(";
      if (is_bounded_topic_struct(type_node, encoding, key_only, keys, info)) {
        size_t size = 0;
        if (key_only) {
          if (!iterate_over_keys(encoding_unaligned_cdr, node, name, info, keys,
                idl_max_serialized_size_iteration, &size, 0, 0)) {
            return false;
          }
        } else {
          for (Fields::Iterator i = fields.begin(); i != fields_end; ++i) {
            idl_max_serialized_size(encoding, size, (*i)->field_type());
          }
        }
        be_global->header_ << size;
      }
      be_global->header_ << ");\n";
    }
    be_global->header_ <<
      "    default:\n"
      "      OPENDDS_ASSERT(false);\n"
      "      return SerializedSizeBound();\n"
      "    }\n"
      "  }\n"
      "\n";

    return true;
  }

  bool generate_marshal_traits_struct(AST_Structure* node,
    TopicKeys& keys, IDL_GlobalData::DCPS_Data_Type_Info* info = 0)
  {
    return
      generate_marshal_traits_struct_bounds_functions(node, keys, info, false) && // All Fields
      generate_marshal_traits_struct_bounds_functions(node, keys, info, true); // Key Fields
  }

  bool generate_marshal_traits_union(AST_Union* node, bool has_key)
  {
    be_global->header_ <<
      "  static SerializedSizeBound serialized_size_bound(const Encoding& encoding)\n"
      "  {\n"
      "    switch (encoding.kind()) {\n";
    for (unsigned e = 0; e < encoding_count; ++e) {
      const Encoding encoding = static_cast<Encoding>(e);
      be_global->header_ <<
        "    case " << encoding_to_encoding_kind(encoding) << ":\n"
        "      return SerializedSizeBound(";
      if (is_bounded_type(node, encoding)) {
        size_t size = 0;
        idl_max_serialized_size(encoding, size, node);
        be_global->header_ << size;
      }
      be_global->header_ << ");\n";
    }
    be_global->header_ <<
      "    default:\n"
      "      OPENDDS_ASSERT(false);\n"
      "      return SerializedSizeBound();\n"
      "    }\n"
      "  }\n"
      "\n";

    be_global->header_ <<
      "  static SerializedSizeBound key_only_serialized_size_bound(const Encoding& encoding)\n"
      "  {\n"
      "    switch (encoding.kind()) {\n";
    for (unsigned e = 0; e < encoding_count; ++e) {
      const Encoding encoding = static_cast<Encoding>(e);
      size_t size = 0;
      // Union can only have discriminator as key, and the discriminator is always bounded.
      if (has_key) {
        idl_max_serialized_size(encoding, size, node->disc_type());
      }
      be_global->header_ <<
        "    case " << encoding_to_encoding_kind(encoding) << ":\n"
        "      return SerializedSizeBound(" << size << ");\n";
    }
    be_global->header_ <<
      "    default:\n"
      "      OPENDDS_ASSERT(false);\n"
      "      return SerializedSizeBound();\n"
      "    }\n"
      "  }\n"
      "\n";

    return true;
  }

  bool generate_marshal_traits(
    AST_Decl* node, const std::string& cxx,
    const OpenDDS::DataRepresentation& repr, ExtensibilityKind exten,
    TopicKeys& keys, IDL_GlobalData::DCPS_Data_Type_Info* info = 0,
    std::string octetSeqOnly = "")
  {
    std::string export_string;
    if (octetSeqOnly.size()) {
      const ACE_CString exporter = be_global->export_macro();
      if (exporter != "") {
        export_string = string(" ") + exporter.c_str();
      }
    }

    be_global->header_ <<
      "template <>\n"
      "struct" << export_string << " MarshalTraits<" << cxx << "> {\n"
      "  static void representations_allowed_by_type(DDS::DataRepresentationIdSeq& seq)\n"
      "  {\n"
        << fill_datareprseq(repr, "seq", "    ") <<
      "  }\n"
      "\n";

    if (node->node_type() == AST_Decl::NT_struct) {
      if (!generate_marshal_traits_struct(
            dynamic_cast<AST_Structure*>(node), keys, info)) {
        return false;
      }
    } else if (node->node_type() == AST_Decl::NT_union) {
      if (!generate_marshal_traits_union(
            dynamic_cast<AST_Union*>(node), keys.count())) {
        return false;
      }
    } else {
      return false;
    }

    const char* msg_block_fn_decl_end = " { return false; }";
    if (octetSeqOnly.size()) {
      const char* get_len;
      const char* set_len;
      const char* get_buffer;
      const char* buffer_pre = "";
      if (be_global->language_mapping() == BE_GlobalData::LANGMAP_CXX11) {
        get_len = "size";
        set_len = "resize";
        get_buffer = "[0]";
        buffer_pre = "&";
        octetSeqOnly += "()";
      } else {
        get_len = set_len = "length";
        get_buffer = ".get_buffer()";
      }

      be_global->impl_ <<
        "bool MarshalTraits<" << cxx << ">::to_message_block(ACE_Message_Block& mb, "
        "const " << cxx << "& stru)\n"
        "{\n"
        "  if (mb.size(stru." << octetSeqOnly << "." << get_len << "()) != 0) {\n"
        "    return false;\n"
        "  }\n"
        "  return mb.copy(reinterpret_cast<const char*>(" << buffer_pre << "stru."
          << octetSeqOnly << get_buffer << "), stru." << octetSeqOnly << "." << get_len
          << "()) == 0;\n"
        "}\n\n"
        "bool MarshalTraits<" << cxx << ">::from_message_block(" << cxx << "& stru, "
        "const ACE_Message_Block& mb)\n"
        "{\n"
        "  stru." << octetSeqOnly << "." << set_len << "(static_cast<unsigned>(mb.length()));\n"
        "  std::memcpy(" << buffer_pre << "stru." << octetSeqOnly << get_buffer
          << ", mb.rd_ptr(), mb.length());\n"
        "  return true;\n"
        "}\n\n";

      msg_block_fn_decl_end = ";";
    }
    be_global->header_ <<
      "  static bool to_message_block(ACE_Message_Block&, const " << cxx << "&)"
        << msg_block_fn_decl_end << "\n"
      "  static bool from_message_block(" << cxx << "&, const ACE_Message_Block&)"
        << msg_block_fn_decl_end << "\n";

    /*
     * This is used for the CDR header.
     * This is just for the base type, nested types can have different
     * extensibilities.
     */
    be_global->header_ <<
      "  static Extensibility extensibility() { return ";
    switch (exten) {
    case extensibilitykind_final:
      be_global->header_ << "FINAL";
      break;
    case extensibilitykind_appendable:
      be_global->header_ << "APPENDABLE";
      break;
    case extensibilitykind_mutable:
      be_global->header_ << "MUTABLE";
      break;
    default:
      idl_global->err()->misc_error(
        "Unexpected extensibility while generating MarshalTraits", node);
      return false;
    }
    be_global->header_ << "; }\n"
      "};\n";

    return true;
  }

} // anonymous namespace

bool marshal_generator::gen_struct(AST_Structure* node,
                                   UTL_ScopedName* name,
                                   const std::vector<AST_Field*>& fields,
                                   AST_Type::SIZE_TYPE /* size */,
                                   const char* /* repoid */)
{
  NamespaceGuard ng;
  be_global->add_include("dds/DCPS/Serializer.h");
  const string cxx = scoped(name); // name as a C++ class
  const ExtensibilityKind exten = be_global->extensibility(node);
  const OpenDDS::DataRepresentation repr =
    be_global->data_representations(node);

  const bool xcdr = repr.xcdr1 || repr.xcdr2;
  const bool not_final = exten != extensibilitykind_final;
  const bool may_be_parameter_list = exten == extensibilitykind_mutable && xcdr;
  const bool use_cxx11 = be_global->language_mapping() == BE_GlobalData::LANGMAP_CXX11;
  {
    Function set_default("set_default", "void");
    set_default.addArg("stru", cxx + "&");
    set_default.endArgs();
    for (size_t i = 0; i < fields.size(); ++i) {
      string field_name = string("stru.") + fields[i]->local_name()->get_string();
      if (use_cxx11) {
        field_name += "()";
      }
      be_global->impl_ << "  " << type_to_default(fields[i]->field_type(), field_name, fields[i]->field_type()->anonymous());
    }
  }

  for (size_t i = 0; i < LENGTH(special_structs); ++i) {
    if (special_structs[i].check(cxx)) {
      return special_structs[i].gen(cxx);
    }
  }

  FieldInfo::EleLenSet anonymous_seq_generated;
  for (size_t i = 0; i < fields.size(); ++i) {
    if (fields[i]->field_type()->anonymous()) {
      FieldInfo af(*fields[i]);
      if (af.arr_) {
        gen_anonymous_array(af);
      } else if (af.seq_ && af.is_new(anonymous_seq_generated)) {
        gen_anonymous_sequence(af);
      }
    }
  }

  RtpsFieldCustomizer rtpsCustom(cxx);

  {
    Function serialized_size("serialized_size", "void");
    serialized_size.addArg("encoding", "const Encoding&");
    serialized_size.addArg("size", "size_t&");
    serialized_size.addArg("stru", "const " + cxx + "&");
    serialized_size.endArgs();

    if (may_be_parameter_list) {
      /*
       * For parameter lists this is used to hold the total size while
       * size is hijacked for field sizes because of alignment resets.
       */
      be_global->impl_ <<
        "  size_t mutable_running_total = 0;\n";
    }

    std::vector<string> code;
    code.push_back("serialized_size_delimiter(encoding, size);");
    generate_dheader_code(code, not_final, false);

    string expr, intro;
    for (size_t i = 0; i < fields.size(); ++i) {
      AST_Type* field_type = resolveActualType(fields[i]->field_type());
      if (!field_type->in_main_file()
          && field_type->node_type() != AST_Decl::NT_pre_defined) {
        be_global->add_referenced(field_type->file_name().c_str());
      }
      const string field_name = fields[i]->local_name()->get_string(),
        cond = rtpsCustom.getConditional(field_name);
      if (!cond.empty()) {
        expr += "  if (" + cond + ") {\n  ";
      }
      if (may_be_parameter_list) {
        expr +=
          "  serialized_size_parameter_id(encoding, size, mutable_running_total);\n";
      }
      if (!findSizeAnonymous(fields[i], "stru", intro, expr)) {
        expr += findSizeCommon(field_name, fields[i]->field_type(), "stru", intro);
      }
      if (!cond.empty()) {
        expr += "  }\n";
      }
    }
    be_global->impl_ << intro << expr;

    if (may_be_parameter_list) {
      be_global->impl_ <<
        "  serialized_size_list_end_parameter_id(encoding, size, mutable_running_total);\n";
    }
  }
  {
    Function insertion("operator<<", "bool");
    insertion.addArg("strm", "Serializer&");
    insertion.addArg("stru", "const " + cxx + "&");
    insertion.endArgs();

    be_global->impl_ <<
      "  const Encoding& encoding = strm.encoding();\n"
      "  ACE_UNUSED_ARG(encoding);\n";
    std::vector<string> code;
    code.push_back("serialized_size(strm.encoding(), total_size, stru);");
    code.push_back("if (!strm.write_delimiter(total_size)) {");
    code.push_back("  return false;");
    code.push_back("}");
    generate_dheader_code(code, not_final);

    // Write the fields
    string intro = rtpsCustom.preamble_;
    if (may_be_parameter_list) {
      string expr;
      be_global->impl_ <<
        "  size_t size = 0;\n";
      std::ostringstream fields_encode;
      for (size_t i = 0; i < fields.size(); ++i) {
        const unsigned id = be_global->get_id(node, fields[i], static_cast<unsigned>(i));
        const string field_name = fields[i]->local_name()->get_string();
        bool is_key = false;
        be_global->check_key(fields[i], is_key);
        fields_encode << "\n";
        expr = "";
        if (!findSizeAnonymous(fields[i], "stru", intro, expr)) {
          expr += findSizeCommon(field_name, fields[i]->field_type(), "stru", intro);
        }
        fields_encode << expr << "\n";
        expr = "";
        fields_encode << "  if (!strm.write_parameter_id(" << id << ", size" << (is_key ? ", true" : "") << ")) {\n"

          "    return false;\n"
          "  }\n"
          "  size = 0;\n"
          "  if (!";
        if (!streamAnonymous(fields[i], "<<", intro, expr)) {
          expr += streamCommon(field_name, fields[i]->field_type(),
              "<< stru", intro, cxx);
        }
        fields_encode << expr << "\n"
                      ") {\n"
                      "    return false;\n"
                      "  }\n";
      }
      fields_encode <<
        "\n"
        "  if (!strm.write_list_end_parameter_id()) {\n"
        "    return false;\n"
        "  }\n";
      be_global->impl_ <<
        intro <<
        fields_encode.str() << "\n"
        "  return true;\n";
    } else {
      string expr;
      for (size_t i = 0; i < fields.size(); ++i) {
        if (i) expr += "\n    && ";
        const string field_name = fields[i]->local_name()->get_string(),
          cond = rtpsCustom.getConditional(field_name);
        if (!cond.empty()) {
          expr += "(!(" + cond + ") || ";
        }
        if (!streamAnonymous(fields[i], "<<", intro, expr)) {
          expr += streamCommon(field_name, fields[i]->field_type(), "<< stru", intro, cxx);
        }
        if (!cond.empty()) {
          expr += ")";
        }
      }
      be_global->impl_ << intro << "  return " << expr << ";\n";
    }
  }
  {
    Function extraction("operator>>", "bool");
    extraction.addArg("strm", "Serializer&");
    extraction.addArg("stru", cxx + "&");
    extraction.endArgs();
    string intro;
    string expr;
    be_global->impl_ <<
      "  const Encoding& encoding = strm.encoding();\n"
      "  ACE_UNUSED_ARG(encoding);\n";
    std::vector<string> code;
    code.push_back("if (!strm.read_delimiter(total_size)) {");
    code.push_back("  return false;");
    code.push_back("}");
    generate_dheader_code(code, not_final);
    if (not_final) {
      be_global->impl_ <<
        "  const size_t start_pos = strm.pos();\n"
        "  ACE_UNUSED_ARG(start_pos);\n";
    }

    if (may_be_parameter_list) {
      be_global->impl_ <<
        "  unsigned member_id;\n"
        "  size_t field_size;\n"
        "  while (true) {\n";

      if (repr.xcdr2) {
        /**
         * We don't have a PID marking the end in XCDR2 parameter lists, but we
         * have the size, so we need to stop after we hit this offset.
         */
        be_global->impl_ <<
          "\n"
          "    if (strm.pos() - start_pos >= total_size";
        if (repr.not_only_xcdr2()) {
          be_global->impl_ <<
            " &&\n"
            "        strm.encoding().xcdr_version() == Encoding::XCDR_VERSION_2";
        }
        be_global->impl_ <<
          ") {\n"
          "      return true;\n"
          "    }\n";
      }

      be_global->impl_ <<
        "    bool must_understand = false;\n"
        "    if (!strm.read_parameter_id(member_id, field_size, must_understand)) {\n"
        "      return false;\n"
        "    }\n";

      if (repr.xcdr1) {
        be_global->impl_ << "    if (member_id == Serializer::pid_list_end";
        if (repr.not_only_xcdr1()) {
          be_global->impl_ <<
            " &&"
            "        strm.encoding().xcdr_version() == Encoding::XCDR_VERSION_1";
        }
        be_global->impl_ << ") {\n"
          "      return true;\n"
          "    }\n";
      }
      be_global->impl_ << "    const size_t end_of_field = strm.pos() + field_size;\n";

      //be_global->impl_ << "    set_default(stru);\n";
      std::ostringstream cases;
      for (size_t i = 0; i < fields.size(); ++i) {
        const unsigned id = be_global->get_id(node, fields[i], static_cast<unsigned>(i));
        string field_name = string("stru.") + fields[i]->local_name()->get_string();
        cases <<
          "    case " << id << ": {\n"
          "      if (!";
        expr = "";
        if (!streamAnonymous(fields[i], ">>", intro, expr)) {
          expr += streamCommon(fields[i]->local_name()->get_string(), fields[i]->field_type(),
            ">> stru", intro, cxx);
        }
        cases <<
          expr << "\n"
          ") {\n";
        AST_Type* field_type = resolveActualType(fields[i]->field_type());
        Classification fld_cls = classify(field_type);

        if (use_cxx11) {
          field_name += "()";
        }
        if (be_global->try_construct(fields[i]) == tryconstructfailaction_use_default) {
          cases <<
            "        " << type_to_default(field_type, field_name, fields[i]->field_type()->anonymous()) <<
            "        strm.set_construction_status(Serializer::ConstructionSuccessful);\n";
          if (!(fld_cls & CL_STRING)) cases << "        strm.skip(end_of_field - strm.pos());\n";
        } else if ((be_global->try_construct(fields[i]) == tryconstructfailaction_trim) && (fld_cls & CL_BOUNDED) &&
                   ((fld_cls & CL_STRING) || (fld_cls & CL_SEQUENCE))) {
          if ((fld_cls & CL_STRING) && (fld_cls & CL_BOUNDED)) {
            string check_not_empty = use_cxx11 ? "!" + field_name + ".empty()" : field_name + ".in()";
            string get_length = use_cxx11 ? field_name + ".length()" : "ACE_OS::strlen(" + field_name + ".in())";
            string inout = use_cxx11 ? "" : ".inout()";
            if (use_cxx11){
              cases <<
                "        if (strm.good_bit() && " << check_not_empty << " && ("
                        << bounded_arg(field_type) << " < " << get_length << ")) {\n"
                "          " << field_name << inout << ".resize(" << bounded_arg(field_type) <<  ");\n"
                "        }";
            } else {
              cases <<
                "        if (strm.good_bit() && " << check_not_empty << " && ("
                        << bounded_arg(field_type) << " < " << get_length << ")) {\n"
                "          " << field_name << inout << "[" << bounded_arg(field_type) << "] = 0;\n"
                "        }";
            }
            cases <<
              " else {\n"
              "          strm.set_construction_status(Serializer::ElementConstructionFailure);\n"
              "          return false;\n"
              "        }\n";
          } else if (fld_cls & CL_SEQUENCE) {
            cases <<
              "        if(strm.get_construction_status() == Serializer::ElementConstructionFailure) {\n"
              "          return false;\n"
              "        }\n"
              "        strm.set_construction_status(Serializer::ConstructionSuccessful);\n"
              "        strm.skip(end_of_field - strm.pos());\n";
          }
        } else {
          //discard/default
          cases << "        strm.set_construction_status(Serializer::ElementConstructionFailure);\n";
          if (!(fld_cls & CL_STRING)) cases << "        strm.skip(end_of_field - strm.pos());\n";
          cases << "        return false;\n  ";
        }
        cases <<
          "      }\n"
          "      break;\n"
          "    }\n";
      }
      be_global->impl_ << intro <<
        "    switch (member_id) {\n"
        << cases.str() <<
        "    default:\n"
        "      if (must_understand) {\n"
        "        if (DCPS_debug_level >= 8) {\n"
        "          ACE_DEBUG((LM_DEBUG, ACE_TEXT(\"(%P|%t) unknown must_understand field(%u) in "
                     << cxx.c_str() << "\\n\"), member_id));\n" <<
        "        }\n"
        "        return false;\n"
        "      }\n"
        "      strm.skip(field_size);\n"
        "      break;\n"
        "    }\n"
        "  }\n"
        "  return false;\n";
    } else {
      string expr;
      for (size_t i = 0; i < fields.size(); ++i) {
        if (i && exten != extensibilitykind_appendable) {
          expr += "\n    && ";
        }
        // TODO (sonndinh): Integrate with try-construct for when the stream
        // ends before some fields on the reader side get their values.
        if (exten == extensibilitykind_appendable) {
          expr += "  if (strm.encoding().xcdr_version() == Encoding::XCDR_VERSION_2 &&";
          expr += "      strm.pos() - start_pos >= total_size) {\n";
          expr += "    return true;\n";
          expr += "  }\n";
        }
        const string field_name = fields[i]->local_name()->get_string();
        const string cond = rtpsCustom.getConditional(field_name);
        if (!cond.empty()) {
          string prefix = rtpsCustom.preFieldRead(field_name);
          if (exten == extensibilitykind_appendable) {
            if (!prefix.empty()) {
              prefix = prefix.substr(0, prefix.length() - 8);
              expr += "  if (!" + prefix + ") {\n";
              expr += "    return false;\n";
              expr += "  }\n";
            }
            expr += "  if ((" + cond + ") && !";
          } else {
            expr += prefix + "(!(" + cond + ") || ";
          }
        } else if (exten == extensibilitykind_appendable) {
          expr += "  if (!";
        }
        if (!streamAnonymous(fields[i], ">>", intro, expr)) {
          expr += streamCommon(field_name, fields[i]->field_type(), ">> stru", intro, cxx);
        }
        if (exten == extensibilitykind_appendable) {
          expr += ") {\n";
          expr += "    return false;\n";
          expr += "  }\n";
        } else if (!cond.empty()) {
          expr += ")";
        }
      }
      if (exten == extensibilitykind_appendable) {
        expr += "  if (strm.encoding().xcdr_version() == Encoding::XCDR_VERSION_2 &&";
        expr += "      strm.pos() - start_pos < total_size) {\n";
        expr += "    strm.skip(total_size - strm.pos() + start_pos);\n";
        expr += "  }\n";
        expr += "  return true;\n";
        be_global->impl_ << intro << expr;
      } else {
        be_global->impl_ << intro << "  return " << expr << ";\n";
      }
    }
  }

  if (be_global->printer()) {
    be_global->add_include("dds/DCPS/Printer.h");
    PreprocessorIfGuard g("ndef OPENDDS_SAFETY_PROFILE");
    g.extra_newline(true);
    Function shift("operator<<", "std::ostream&");
    shift.addArg("strm", "Printable");
    shift.addArg("stru", "const " + cxx + "&");
    shift.endArgs();
    shift.extra_newline_ = false;
    string intro;
    string expr = "  strm.push_indent();\n";
    for (size_t i = 0; i < fields.size(); ++i) {
      const string field_name = fields[i]->local_name()->get_string();
      AST_Type* const field_type = resolveActualType(fields[i]->field_type());
      const AST_Decl::NodeType node_type = field_type->node_type();
      const bool is_composite_type = node_type == AST_Decl::NT_struct;
      const bool is_string_type = node_type == AST_Decl::NT_string ||
        node_type == AST_Decl::NT_wstring;
      expr +=
        "\n"
        "  // Print " + field_name  + "\n"
        "  strm.print_indent();\n"
        "  if (strm.printer().print_field_names()) {\n"
        "    strm.os() << \"" + field_name + ":";
      if (is_composite_type) {
        expr += "\" << std::endl";
      } else {
        expr += " \"";
      }
      expr += ";\n"
        "  }\n";
      if (is_string_type) {
        expr +=
          "  strm.os() << '\"';\n";
      }
      expr +=
        "  " + streamCommon(
          field_name, fields[i]->field_type(), "<< stru", intro, cxx, true);
      if (is_string_type) {
        expr += " << '\"'";
      }
      if (!is_composite_type) {
        expr += " << std::endl";
      }
      expr += ";\n";
    }
    be_global->impl_ << intro << expr <<
      "\n"
      "  return strm.os();\n";
  }

  IDL_GlobalData::DCPS_Data_Type_Info* info = idl_global->is_dcps_type(name);
  const bool is_topic_type = be_global->is_topic_type(node);
  TopicKeys keys;
  if (is_topic_type) {
    keys = TopicKeys(node);
    info = 0; // Annotations Override DCPS_DATA_TYPE
  }

  // Only generate these methods if this is a topic type
  if (info || is_topic_type) {
    std::string octetSeqOnly;
    if (fields.size() == 1) {
      AST_Type* const type = resolveActualType(fields[0]->field_type());
      const Classification fld_cls = classify(type);
      if (fld_cls & CL_SEQUENCE) {
        AST_Sequence* const seq = dynamic_cast<AST_Sequence*>(type);
        AST_Type* const base = seq->base_type();
        if (classify(base) & CL_PRIMITIVE) {
          AST_PredefinedType* const pt = dynamic_cast<AST_PredefinedType*>(base);
          if (pt->pt() == AST_PredefinedType::PT_octet) {
            octetSeqOnly = fields[0]->local_name()->get_string();
          }
        }
      }
    }

    {
      Function serialized_size("serialized_size", "void");
      serialized_size.addArg("encoding", "const Encoding&");
      serialized_size.addArg("size", "size_t&");
      serialized_size.addArg("stru", "const KeyOnly<const " + cxx + ">");
      serialized_size.endArgs();

      be_global->impl_ <<
        "  switch (encoding.xcdr_version()) {\n";
      for (unsigned e = 0; e < encoding_count; ++e) {
        string expr, intro;
        const Encoding encoding = static_cast<Encoding>(e);
        if (!iterate_over_keys(encoding, node, cxx, info, keys,
            serialized_size_iteration, 0, &expr, &intro)) {
          return false;
        }
        be_global->impl_ <<
          "  case " << encoding_to_xcdr_version(encoding) << ":\n"
          "    {\n"
          << intro << expr <<
          "    break;\n"
          "    }\n";
      }
      be_global->impl_ <<
        "  }\n";
    }

    {
      Function insertion("operator<<", "bool");
      insertion.addArg("strm", "Serializer&");
      insertion.addArg("stru", "KeyOnly<const " + cxx + ">");
      insertion.endArgs();

      bool first = true;
      string expr, intro;

      if (info) {
        IDL_GlobalData::DCPS_Data_Type_Info_Iter iter(info->key_list_);
        for (ACE_TString* kp = 0; iter.next(kp) != 0; iter.advance()) {
          const string key_name = ACE_TEXT_ALWAYS_CHAR(kp->c_str());
          AST_Type* field_type = 0;
          try {
            field_type = find_type(node, key_name);
          } catch (const string& error) {
            std::cerr << "ERROR: Invalid key specification for " << cxx
                      << " (" << key_name << "). " << error << std::endl;
            return false;
          }
          if (first) {
            first = false;
          } else {
            expr += "\n    && ";
          }
          expr += streamCommon(key_name, field_type, "<< stru.t", intro);
        }
      } else {
        const TopicKeys::Iterator finished = keys.end();
        for (TopicKeys::Iterator i = keys.begin(); i != finished; ++i) {
          std::string key_access = i.path();
          if (first) {
            first = false;
          } else {
            expr += "\n    && ";
          }
          AST_Type* straight_ast_type = i.get_ast_type();
          AST_Type* ast_type;
          if (i.root_type() == TopicKeys::UnionType) {
            key_access.append("._d()");
            AST_Union* union_type = dynamic_cast<AST_Union*>(straight_ast_type);
            if (!union_type) {
              std::cerr << "ERROR: Invalid key iterator for: " << cxx;
              return false;
            }
            ast_type = dynamic_cast<AST_Type*>(union_type->disc_type());
          } else {
            ast_type = straight_ast_type;
          }
          if (!ast_type) {
            std::cerr << "ERROR: Invalid key iterator for: " << cxx;
            return false;
          }
          expr += streamCommon(key_access, ast_type, "<< stru.t", intro);
        }
      }

      if (first) {
        be_global->impl_ << intro << "  return true;\n";
      } else {
        be_global->impl_ << intro << "  return " << expr << ";\n";
      }
    }

    {
      Function extraction("operator>>", "bool");
      extraction.addArg("strm", "Serializer&");
      extraction.addArg("stru", "KeyOnly<" + cxx + ">");
      extraction.endArgs();

      bool first = true;
      string expr, intro;

      if (info) {
        IDL_GlobalData::DCPS_Data_Type_Info_Iter iter(info->key_list_);
        for (ACE_TString* kp = 0; iter.next(kp) != 0; iter.advance()) {
          const string key_name = ACE_TEXT_ALWAYS_CHAR(kp->c_str());
          AST_Type* field_type = 0;
          try {
            field_type = find_type(node, key_name);
          } catch (const string& error) {
            std::cerr << "ERROR: Invalid key specification for " << cxx
                      << " (" << key_name << "). " << error << std::endl;
            return false;
          }
          if (first) {
            first = false;
          } else {
            expr += "\n    && ";
          }
          expr += streamCommon(key_name, field_type, ">> stru.t", intro);
        }
      } else {
        const TopicKeys::Iterator finished = keys.end();
        for (TopicKeys::Iterator i = keys.begin(); i != finished; ++i) {
          const std::string key_name = i.path();
          AST_Type* ast_type = i.get_ast_type();
          if (i.root_type() == TopicKeys::UnionType) {
            AST_Union* union_type = dynamic_cast<AST_Union*>(ast_type);
            if (!union_type) {
              std::cerr << "ERROR: Invalid key iterator for: " << cxx;
              return false;
            }
            AST_Type* disc_type = dynamic_cast<AST_Type*>(union_type->disc_type());
            if (!disc_type) {
              std::cerr << "ERROR: Invalid key iterator for: " << cxx;
              return false;
            }
            be_global->impl_ <<
              "  {\n"
              "    " << scoped(disc_type->name()) << " tmp;\n" <<
              "    if (strm >> " << getWrapper("tmp", disc_type, WD_INPUT) << ") {\n"
              "      stru.t." << key_name << "._d(tmp);\n"
              "    } else {\n"
              "      return false;\n"
              "    }\n"
              "  }\n"
              ;
          } else {
            if (first) {
              first = false;
            } else {
              expr += "\n    && ";
            }
            expr += streamCommon(key_name, ast_type, ">> stru.t", intro);
          }
        }
      }

      if (first) {
        be_global->impl_ << intro << "  return true;\n";
      } else {
        be_global->impl_ << intro << "  return " << expr << ";\n";
      }
    }

    if (!generate_marshal_traits(node, cxx, repr, exten, keys, info, octetSeqOnly)) {
      return false;
    }
  }

  return true;
}

namespace {

  bool isRtpsSpecialUnion(const string& cxx)
  {
    return cxx == "OpenDDS::RTPS::Parameter"
      || cxx == "OpenDDS::RTPS::Submessage";
  }

  bool genRtpsParameter(AST_Type* discriminator,
                        const std::vector<AST_UnionBranch*>& branches)
  {
    const string cxx = "OpenDDS::RTPS::Parameter";
    {
      Function serialized_size("serialized_size", "void");
      serialized_size.addArg("encoding", "const Encoding&");
      serialized_size.addArg("size", "size_t&");
      serialized_size.addArg("uni", "const " + cxx + "&");
      serialized_size.endArgs();
      generateSwitchForUnion("uni._d()", findSizeCommon, branches,
                             discriminator, "", "", cxx.c_str());
      be_global->impl_ <<
        "  size += 4; // parameterId & length\n";
    }
    {
      Function insertion("operator<<", "bool");
      insertion.addArg("outer_strm", "Serializer&");
      insertion.addArg("uni", "const " + cxx + "&");
      insertion.endArgs();
      be_global->impl_ <<
        "  if (!(outer_strm << uni._d())) {\n"
        "    return false;\n"
        "  }\n"
        "  size_t size = serialized_size(outer_strm.encoding(), uni);\n"
        "  size -= 4; // parameterId & length\n"
        "  const size_t post_pad = 4 - (size % 4);\n"
        "  const size_t total = size + ((post_pad < 4) ? post_pad : 0);\n"
        "  if (size > ACE_UINT16_MAX || "
        "!(outer_strm << ACE_CDR::UShort(total))) {\n"
        "    return false;\n"
        "  }\n"
        "  ACE_Message_Block param(size);\n"
        "  Serializer strm(&param, outer_strm.encoding());"
        "  if (!insertParamData(strm, uni)) {\n"
        "    return false;\n"
        "  }\n"
        "  const ACE_CDR::Octet* data = reinterpret_cast<ACE_CDR::Octet*>("
        "param.rd_ptr());\n"
        "  if (!outer_strm.write_octet_array(data, ACE_CDR::ULong(param.length()))) {\n"
        "    return false;\n"
        "  }\n"
        "  if (post_pad < 4 && outer_strm.encoding().alignment() != "
        "Encoding::ALIGN_NONE) {\n"
        "    static const ACE_CDR::Octet padding[3] = {0};\n"
        "    return outer_strm.write_octet_array(padding, "
        "ACE_CDR::ULong(post_pad));\n"
        "  }\n"
        "  return true;\n";
    }
    {
      Function insertData("insertParamData", "bool");
      insertData.addArg("strm", "Serializer&");
      insertData.addArg("uni", "const " + cxx + "&");
      insertData.endArgs();
      generateSwitchForUnion("uni._d()", streamCommon, branches, discriminator,
                             "return", "<< ", cxx.c_str());
    }
    {
      Function extraction("operator>>", "bool");
      extraction.addArg("outer_strm", "Serializer&");
      extraction.addArg("uni", cxx + "&");
      extraction.endArgs();
      be_global->impl_ <<
        "  ACE_CDR::UShort disc, size;\n"
        "  if (!(outer_strm >> disc) || !(outer_strm >> size)) {\n"
        "    return false;\n"
        "  }\n"
        "  if (disc == OpenDDS::RTPS::PID_SENTINEL) {\n"
        "    uni._d(OpenDDS::RTPS::PID_SENTINEL);\n"
        "    return true;\n"
        "  }\n"
        "  ACE_Message_Block param(size);\n"
        "  ACE_CDR::Octet* data = reinterpret_cast<ACE_CDR::Octet*>("
        "param.wr_ptr());\n"
        "  if (!outer_strm.read_octet_array(data, size)) {\n"
        "    return false;\n"
        "  }\n"
        "  param.wr_ptr(size);\n"
        "  const Encoding encoding(\n"
        "    Encoding::KIND_XCDR1, outer_strm.swap_bytes());\n"
        "  Serializer strm(&param, encoding);"
        "  switch (disc) {\n";
      generateSwitchBody(streamCommon, branches, discriminator,
                         "return", ">> ", cxx.c_str(), true);
      be_global->impl_ <<
        "  default:\n"
        "    {\n"
        "      uni.unknown_data(DDS::OctetSeq(size));\n"
        "      uni.unknown_data().length(size);\n"
        "      std::memcpy(uni.unknown_data().get_buffer(), data, size);\n"
        "      uni._d(disc);\n"
        "    }\n"
        "  }\n"
        "  return true;\n";
    }
    return true;
  }

  bool genRtpsSubmessage(AST_Type* discriminator,
                         const std::vector<AST_UnionBranch*>& branches)
  {
    const string cxx = "OpenDDS::RTPS::Submessage";
    {
      Function serialized_size("serialized_size", "void");
      serialized_size.addArg("encoding", "const Encoding&");
      serialized_size.addArg("size", "size_t&");
      serialized_size.addArg("uni", "const " + cxx + "&");
      serialized_size.endArgs();
      generateSwitchForUnion("uni._d()", findSizeCommon, branches,
                             discriminator, "", "", cxx.c_str());
    }
    {
      Function insertion("operator<<", "bool");
      insertion.addArg("strm", "Serializer&");
      insertion.addArg("uni", "const " + cxx + "&");
      insertion.endArgs();
      generateSwitchForUnion("uni._d()", streamCommon, branches,
                             discriminator, "return", "<< ", cxx.c_str());
    }
    {
      Function insertion("operator>>", "bool");
      insertion.addArg("strm", "Serializer&");
      insertion.addArg("uni", cxx + "&");
      insertion.endArgs();
      be_global->impl_ << "  // unused\n  return false;\n";
    }
    return true;
  }

  bool genRtpsSpecialUnion(const string& cxx, AST_Type* discriminator,
                           const std::vector<AST_UnionBranch*>& branches)
  {
    if (cxx == "OpenDDS::RTPS::Parameter") {
      return genRtpsParameter(discriminator, branches);
    } else if (cxx == "OpenDDS::RTPS::Submessage") {
      return genRtpsSubmessage(discriminator, branches);
    } else {
      return false;
    }
  }
}

bool marshal_generator::gen_union(AST_Union* node, UTL_ScopedName* name,
   const std::vector<AST_UnionBranch*>& branches, AST_Type* discriminator,
   const char*)
{
  NamespaceGuard ng;
  be_global->add_include("dds/DCPS/Serializer.h");
  string cxx = scoped(name); // name as a C++ class
  Classification disc_cls = classify(discriminator);

  const ExtensibilityKind exten = be_global->extensibility(node);
  const OpenDDS::DataRepresentation repr =
    be_global->data_representations(node);

  const bool not_final = exten != extensibilitykind_final;

  {
    Function set_default("set_default", "void");
    set_default.addArg("uni", cxx + "&");
    set_default.endArgs();
    be_global->impl_ << "  " << scoped(discriminator->name()) << " temp;\n";
    be_global->impl_ << type_to_default(discriminator, "  temp");
    be_global->impl_ << "  uni._d(temp);\n";
    AST_Type* disc_type = resolveActualType(discriminator);
    Classification disc_cls = classify(disc_type);
    ACE_CDR::ULong default_enum_val = 0;
    if (disc_cls & CL_ENUM) {
      AST_Enum* enu = dynamic_cast<AST_Enum*>(disc_type);
      UTL_ScopeActiveIterator i(enu, UTL_Scope::IK_decls);
      AST_EnumVal *item = dynamic_cast<AST_EnumVal*>(i.item());
      default_enum_val = item->constant_value()->ev()->u.eval;
    }
    bool found = false;
    for (std::vector<AST_UnionBranch*>::const_iterator itr = branches.begin(); itr < branches.end() && !found; itr++) {
      AST_UnionBranch* branch = *itr;
      for (unsigned i = 0; i < branch->label_list_length(); i++) {
        AST_UnionLabel* ul = branch->label(i);
        if (ul->label_kind() != AST_UnionLabel::UL_default) {
          AST_Expression::AST_ExprValue* ev = branch->label(i)->label_val()->ev();
          if ((ev->et == AST_Expression::EV_enum && ev->u.eval == default_enum_val) ||
              (ev->et == AST_Expression::EV_short && ev->u.sval == 0) ||
              (ev->et == AST_Expression::EV_ushort && ev->u.usval == 0) ||
              (ev->et == AST_Expression::EV_long && ev->u.lval == 0) ||
              (ev->et == AST_Expression::EV_ulong && ev->u.ulval == 0) ||
              (ev->et == AST_Expression::EV_longlong && ev->u.llval == 0) ||
              (ev->et == AST_Expression::EV_ulonglong && ev->u.ullval == 0) ||
              (ev->et == AST_Expression::EV_float && ev->u.fval == 0) ||
              (ev->et == AST_Expression::EV_double && ev->u.dval == 0) ||
              (ev->et == AST_Expression::EV_longdouble && ev->u.sval == 0) ||
              (ev->et == AST_Expression::EV_char && ev->u.cval == 0) ||
              (ev->et == AST_Expression::EV_wchar && ev->u.wcval == 0) ||
              (ev->et == AST_Expression::EV_octet && ev->u.oval == 0) ||
              (ev->et == AST_Expression::EV_bool && ev->u.bval == 0))
          {
            be_global->impl_ << "  " << scoped(branch->field_type()->name()) << " btemp;\n";
            be_global->impl_ << type_to_default(branch->field_type(), "btemp");
            be_global->impl_ << "  uni." << branch->local_name()->get_string() << "(btemp);\n";
            found = true;
            break;
          }
        }
      }
    }
  }

  for (size_t i = 0; i < LENGTH(special_unions); ++i) {
    if (special_unions[i].check(cxx)) {
      return special_unions[i].gen(cxx, discriminator, branches);
    }
  }

  const string wrap_out = getWrapper("uni._d()", discriminator, WD_OUTPUT);
  {
    Function serialized_size("serialized_size", "void");
    serialized_size.addArg("encoding", "const Encoding&");
    serialized_size.addArg("size", "size_t&");
    serialized_size.addArg("uni", "const " + cxx + "&");
    serialized_size.endArgs();

    std::vector<string> code;
    code.push_back("serialized_size_delimiter(encoding, size);");
    generate_dheader_code(code, not_final, false);

    if (exten == extensibilitykind_mutable) {
      be_global->impl_ <<
        "  size_t mutable_running_total = 0;\n"
        "  serialized_size_parameter_id(encoding, size, mutable_running_total);\n";
    }

    if (disc_cls & CL_ENUM) {
      be_global->impl_ <<
        "  OpenDDS::DCPS::primitive_serialized_size_ulong(encoding, size);\n";
    } else {
      be_global->impl_ <<
        "  primitive_serialized_size(encoding, size, " << wrap_out << ");\n";
    }

    if (exten == extensibilitykind_mutable) {
      be_global->impl_ <<
        "  serialized_size_parameter_id(encoding, size, mutable_running_total);\n";
    }

    generateSwitchForUnion("uni._d()", findSizeCommon, branches, discriminator,
                           "", "", cxx.c_str());

    if (exten == extensibilitykind_mutable) {
      be_global->impl_ <<
        "  serialized_size_list_end_parameter_id(encoding, size, mutable_running_total);\n";
    }
  }
  {
    Function insertion("operator<<", "bool");
    insertion.addArg("strm", "Serializer&");
    insertion.addArg("uni", "const " + cxx + "&");
    insertion.endArgs();

    be_global->impl_ <<
      "  const Encoding& encoding = strm.encoding();\n"
      "  ACE_UNUSED_ARG(encoding);\n";
    std::vector<string> code;
    code.push_back("serialized_size(encoding, total_size, uni);");
    code.push_back("if (!strm.write_delimiter(total_size)) {");
    code.push_back("  return false;");
    code.push_back("}");
    generate_dheader_code(code, not_final);

    // EMHEADER for discriminator
    if (exten == extensibilitykind_mutable) {
      be_global->impl_ <<
        "  size_t size = 0;\n";

      if (disc_cls & CL_ENUM) {
        be_global->impl_ <<
          "  primitive_serialized_size_ulong(encoding, size);\n";
      } else {
        be_global->impl_ <<
          "  primitive_serialized_size(encoding, size, " << wrap_out << ");\n";
      }

      be_global->impl_ <<
        "  if (!strm.write_parameter_id(0, size)) {\n"
        "    return false;\n"
        "  }\n"
        "  size = 0;\n";
    }

    be_global->impl_ <<
      streamAndCheck("<< " + wrap_out);
    if (generateSwitchForUnion("uni._d()", streamCommon, branches,
                               discriminator, "return", "<< ", cxx.c_str(),
                               false, true, true,
                               exten == extensibilitykind_mutable ? findSizeCommon : 0,
                               exten == extensibilitykind_mutable ? node : 0)) {
      be_global->impl_ <<
        "  return true;\n";
    }
  }
  {
    Function extraction("operator>>", "bool");
    extraction.addArg("strm", "Serializer&");
    extraction.addArg("uni", cxx + "&");
    extraction.endArgs();

    be_global->impl_ <<
      "  const Encoding& encoding = strm.encoding();\n"
      "  ACE_UNUSED_ARG(encoding);\n";
    std::vector<string> code;
    code.push_back("if (!strm.read_delimiter(total_size)) {");
    code.push_back("  return false;");
    code.push_back("}");
    generate_dheader_code(code, not_final);

    if (exten == extensibilitykind_mutable) {
      // EMHEADER for discriminator
      be_global->impl_ <<
        "  unsigned member_id;\n"
        "  size_t field_size;\n"
        "  bool must_understand = false;\n"
        "  if (!strm.read_parameter_id(member_id, field_size, must_understand)) {\n"
        "    return false;\n"
        "  }\n";
      AST_Annotation_Appl* ann_appl = node->disc_annotations().find("::@try_construct");
      TryConstructFailAction try_construct = get_try_construct_annotation(ann_appl);
      be_global->impl_ <<
        "  " << scoped(discriminator->name()) << " disc;\n" 
        "  if (!(strm >> disc)) {\n";
      if (try_construct == tryconstructfailaction_use_default) {
        be_global->impl_ <<
          "    set_default(uni);\n"
          "    if (!strm.read_parameter_id(member_id, field_size, must_understand)) {\n"
          "      return false;\n"
          "    }\n"
          "    strm.skip(field_size);\n"
          "    strm.set_construction_status(Serializer::ConstructionSuccessful);\n"
          "    return true;\n";
      } else {
        be_global->impl_ <<
          "    if (!strm.read_parameter_id(member_id, field_size, must_understand)) {\n"
          "      return false;\n"
          "    }\n"
          "    strm.skip(field_size);\n"
          "    strm.set_construction_status(Serializer::ElementConstructionFailure);\n"
          "    return false;\n";
      }
      be_global->impl_ << "  }\n";

      be_global->impl_ <<
        "  member_id = 0;\n"
        "  field_size = 0;\n"
        "  must_understand = false;\n"
        "  if (!strm.read_parameter_id(member_id, field_size, must_understand)) {\n"
        "    return false;\n"
        "  }\n";

      if (generateSwitchForUnion("disc", streamCommon, branches,
        discriminator, "if", ">> ", cxx.c_str(), false, true, true, 0)) {
        be_global->impl_ <<
          "  return true;\n";
      }
    } else {
      be_global->impl_ <<
        "  " << scoped(discriminator->name()) << " disc;\n" <<
        streamAndCheck(">> " + getWrapper("disc", discriminator, WD_INPUT));
      if (generateSwitchForUnion("disc", streamCommon, branches,
          discriminator, "if", ">> ", cxx.c_str())) {
        be_global->impl_ <<
          "  return true;\n";
      }
    }
  }

  const bool has_key = be_global->has_key(node);
  const bool is_topic_type = be_global->is_topic_type(node);

  if (!is_topic_type) {
    if (has_key) {
      idl_global->err()->misc_warning(
        "Union has @key on its discriminator, "
        "but it's not a topic type, ignoring it...", node);
    }
    return true;
  }

  const string key_only_wrap_out = getWrapper("uni.t._d()", discriminator, WD_OUTPUT);

  {
    Function serialized_size("serialized_size", "void");
    serialized_size.addArg("encoding", "const Encoding&");
    serialized_size.addArg("size", "size_t&");
    serialized_size.addArg("uni", "const KeyOnly<const " + cxx + ">");
    serialized_size.endArgs();

    if (has_key) {
      std::vector<string> code;
      code.push_back("serialized_size_delimiter(encoding, size);");
      generate_dheader_code(code, not_final, false);

      if (exten == extensibilitykind_mutable) {
        be_global->impl_ <<
          "  size_t mutable_running_total = 0;\n"
          "  serialized_size_parameter_id(encoding, size, mutable_running_total);\n";
      }

      if (disc_cls & CL_ENUM) {
        be_global->impl_ <<
          "  OpenDDS::DCPS::primitive_serialized_size_ulong(encoding, size);\n";
      } else {
        be_global->impl_ <<
          "  primitive_serialized_size(encoding, size, " << key_only_wrap_out << ");\n";
      }

      if (exten == extensibilitykind_mutable) {
        be_global->impl_ <<
          "  serialized_size_list_end_parameter_id(encoding, size, mutable_running_total);\n";
      }
    }
  }

  {
    Function insertion("operator<<", "bool");
    insertion.addArg("strm", "Serializer&");
    insertion.addArg("uni", "KeyOnly<const " + cxx + ">");
    insertion.endArgs();

    if (has_key) {
      be_global->impl_ << "  const Encoding& encoding = strm.encoding();\n";
      std::vector<string> code;
      code.push_back("serialized_size(encoding, total_size, uni);");
      code.push_back("if (!strm.write_delimiter(total_size)) {");
      code.push_back("  return false;");
      code.push_back("}");
      generate_dheader_code(code, not_final);

      // EMHEADER for discriminator
      if (exten == extensibilitykind_mutable) {
        be_global->impl_ <<
          "  size_t size = 0;\n";

        if (disc_cls & CL_ENUM) {
          be_global->impl_ <<
            "  primitive_serialized_size_ulong(encoding, size);\n";
        } else {
          be_global->impl_ <<
            "  primitive_serialized_size(encoding, size, " << wrap_out << ");\n";
        }

        be_global->impl_ <<
          "  if (!strm.write_parameter_id(0, size)) {\n"
          "    return false;\n"
          "  }\n"
          "  size = 0;\n";
      }

      be_global->impl_ << streamAndCheck("<< " + key_only_wrap_out);
    }

    be_global->impl_ << "  return true;\n";
  }

  {
    Function extraction("operator>>", "bool");
    extraction.addArg("strm", "Serializer&");
    extraction.addArg("uni", "KeyOnly<" + cxx + ">");
    extraction.endArgs();

    if (has_key) {
      // DHEADER
      be_global->impl_ << "  const Encoding& encoding = strm.encoding();\n";
      std::vector<string> code;
      code.push_back("if (!strm.read_delimiter(total_size)) {");
      code.push_back("  return false;");
      code.push_back("}");
      generate_dheader_code(code, not_final);

      if (exten == extensibilitykind_mutable) {
        // EMHEADER for discriminator
        be_global->impl_ <<
          "  unsigned member_id;\n"
          "  size_t field_size;\n"
          "  bool must_understand = false;\n"
          "  if (!strm.read_parameter_id(member_id, field_size, must_understand)) {\n"
          "    return false;\n"
          "  }\n";
      }

      be_global->impl_
        << "  " << scoped(discriminator->name()) << " disc;\n"
        << streamAndCheck(">> " + getWrapper("disc", discriminator, WD_INPUT))
        << "  uni.t._d(disc);\n";
    }

    be_global->impl_ << "  return true;\n";
  }

  TopicKeys keys(node);
  return generate_marshal_traits(node, cxx, repr, exten, keys);
}
