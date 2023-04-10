#include <store.h>
#include "field.h"
#include "magic_enum.hpp"
#include "text_embedder_manager.h"
#include <stack>
#include <collection_manager.h>
#include <regex>

Option<bool> field::json_field_to_field(bool enable_nested_fields, nlohmann::json& field_json,
                                        std::vector<field>& the_fields,
                                        string& fallback_field_type, size_t& num_auto_detect_fields) {

    if(field_json["name"] == "id") {
        // No field should exist with the name "id" as it is reserved for internal use
        // We cannot throw an error here anymore since that will break backward compatibility!
        LOG(WARNING) << "Collection schema cannot contain a field with name `id`. Ignoring field.";
        return Option<bool>(true);
    }

    if(!field_json.is_object() ||
       field_json.count(fields::name) == 0 || field_json.count(fields::type) == 0 ||
       !field_json.at(fields::name).is_string() || !field_json.at(fields::type).is_string()) {

        return Option<bool>(400, "Wrong format for `fields`. It should be an array of objects containing "
                                 "`name`, `type`, `optional` and `facet` properties.");
    }

    if(field_json.count("drop") != 0) {
        return Option<bool>(400, std::string("Invalid property `drop` on field `") +
                                 field_json[fields::name].get<std::string>() + std::string("`: it is allowed only "
                                                                                           "during schema update."));
    }

    if(field_json.count(fields::facet) != 0 && !field_json.at(fields::facet).is_boolean()) {
        return Option<bool>(400, std::string("The `facet` property of the field `") +
                                 field_json[fields::name].get<std::string>() + std::string("` should be a boolean."));
    }

    if(field_json.count(fields::optional) != 0 && !field_json.at(fields::optional).is_boolean()) {
        return Option<bool>(400, std::string("The `optional` property of the field `") +
                                 field_json[fields::name].get<std::string>() + std::string("` should be a boolean."));
    }

    if(field_json.count(fields::index) != 0 && !field_json.at(fields::index).is_boolean()) {
        return Option<bool>(400, std::string("The `index` property of the field `") +
                                 field_json[fields::name].get<std::string>() + std::string("` should be a boolean."));
    }

    if(field_json.count(fields::sort) != 0 && !field_json.at(fields::sort).is_boolean()) {
        return Option<bool>(400, std::string("The `sort` property of the field `") +
                                 field_json[fields::name].get<std::string>() + std::string("` should be a boolean."));
    }

    if(field_json.count(fields::infix) != 0 && !field_json.at(fields::infix).is_boolean()) {
        return Option<bool>(400, std::string("The `infix` property of the field `") +
                                 field_json[fields::name].get<std::string>() + std::string("` should be a boolean."));
    }

    if(field_json.count(fields::locale) != 0){
        if(!field_json.at(fields::locale).is_string()) {
            return Option<bool>(400, std::string("The `locale` property of the field `") +
                                     field_json[fields::name].get<std::string>() + std::string("` should be a string."));
        }

        if(!field_json[fields::locale].get<std::string>().empty() &&
           field_json[fields::locale].get<std::string>().size() != 2) {
            return Option<bool>(400, std::string("The `locale` value of the field `") +
                                     field_json[fields::name].get<std::string>() + std::string("` is not valid."));
        }
    }

    if (field_json.count(fields::reference) != 0 && !field_json.at(fields::reference).is_string()) {
        return Option<bool>(400, "Reference should be a string.");
    } else if (field_json.count(fields::reference) == 0) {
        field_json[fields::reference] = "";
    }

    if(field_json["name"] == ".*") {
        if(field_json.count(fields::facet) == 0) {
            field_json[fields::facet] = false;
        }

        if(field_json.count(fields::optional) == 0) {
            field_json[fields::optional] = true;
        }

        if(field_json.count(fields::index) == 0) {
            field_json[fields::index] = true;
        }

        if(field_json.count(fields::locale) == 0) {
            field_json[fields::locale] = "";
        }

        if(field_json.count(fields::sort) == 0) {
            field_json[fields::sort] = false;
        }

        if(field_json.count(fields::infix) == 0) {
            field_json[fields::infix] = false;
        }

        if(field_json[fields::optional] == false) {
            return Option<bool>(400, "Field `.*` must be an optional field.");
        }

        if(field_json[fields::facet] == true) {
            return Option<bool>(400, "Field `.*` cannot be a facet field.");
        }

        if(field_json[fields::index] == false) {
            return Option<bool>(400, "Field `.*` must be an index field.");
        }

        if (!field_json[fields::reference].get<std::string>().empty()) {
            return Option<bool>(400, "Field `.*` cannot be a reference field.");
        }

        field fallback_field(field_json["name"], field_json["type"], field_json["facet"],
                             field_json["optional"], field_json[fields::index], field_json[fields::locale],
                             field_json[fields::sort], field_json[fields::infix]);

        if(fallback_field.has_valid_type()) {
            fallback_field_type = fallback_field.type;
            num_auto_detect_fields++;
        } else {
            return Option<bool>(400, "The `type` of field `.*` is invalid.");
        }

        the_fields.emplace_back(fallback_field);
        return Option<bool>(true);
    }

    if(field_json.count(fields::facet) == 0) {
        field_json[fields::facet] = false;
    }

    if(field_json.count(fields::index) == 0) {
        field_json[fields::index] = true;
    }

    if(field_json.count(fields::locale) == 0) {
        field_json[fields::locale] = "";
    }

    if(field_json.count(fields::sort) == 0) {
        if(field_json["type"] == field_types::INT32 || field_json["type"] == field_types::INT64 ||
           field_json["type"] == field_types::FLOAT || field_json["type"] == field_types::BOOL ||
           field_json["type"] == field_types::GEOPOINT || field_json["type"] == field_types::GEOPOINT_ARRAY) {
            if(field_json.count(fields::num_dim) == 0) {
                field_json[fields::sort] = true;
            } else {
                field_json[fields::sort] = false;
            }
        } else {
            field_json[fields::sort] = false;
        }
    }

    if(field_json.count(fields::infix) == 0) {
        field_json[fields::infix] = false;
    }

    if(field_json[fields::type] == field_types::OBJECT || field_json[fields::type] == field_types::OBJECT_ARRAY) {
        if(!enable_nested_fields) {
            return Option<bool>(400, "Type `object` or `object[]` can be used only when nested fields are enabled by "
                                     "setting` enable_nested_fields` to true.");
        }
    }

    if(field_json.count(fields::embed) != 0) {
        // If the model path is not specified, use the default model and set the number of dimensions to 384 (number of dimensions of the default model)
        field_json[fields::num_dim] = static_cast<unsigned int>(384);

        auto& embed_json = field_json[fields::embed];

        if(embed_json.count(fields::from) == 0) {
            return Option<bool>(400, "Property `" + fields::embed + "." + fields::from + "` not found.");
        }

        if(embed_json.count(fields::model_config) == 0) {
            return Option<bool>(400, "Property `" + fields::embed + "." + fields::model_config + "` not found.");
        }

        auto& model_config = embed_json[fields::model_config];
        
        if(model_config.count(fields::model_name) == 0) {
            return Option<bool>(400, "Property `" + fields::embed + "." + fields::model_config + "." + fields::model_name + "`not found");
        }

        unsigned int num_dim = 0;
        if(!model_config[fields::model_name].is_string()) {
            return Option<bool>(400, "Property `" + fields::embed + "."  + fields::model_config + "." + fields::model_name + "` must be a string.");
        }
        if(model_config[fields::model_name].get<std::string>().empty()) {
            return Option<bool>(400, "Property `" + fields::embed + "." + fields::model_config + "." + fields::model_name + "` cannot be empty.");
        }

        if(model_config.count(fields::indexing_prefix) != 0) {
            if(!model_config[fields::indexing_prefix].is_string()) {
                return Option<bool>(400, "Property `" + fields::embed + "." + fields::model_config + "." + fields::indexing_prefix + "` must be a string.");
            }
        }

        if(model_config.count(fields::query_prefix) != 0) {
            if(!model_config[fields::query_prefix].is_string()) {
                return Option<bool>(400, "Property `" + fields::embed + "." + fields::model_config + "." + fields::query_prefix + "` must be a string.");
            }
        }

        auto res = TextEmbedder::is_model_valid(model_config, num_dim);
        if(!res.ok()) {
            return Option<bool>(res.code(), res.error());
        }
        field_json[fields::num_dim] = num_dim;
    } else {
        field_json[fields::embed] = nlohmann::json::object();
    }

    auto DEFAULT_VEC_DIST_METRIC = magic_enum::enum_name(vector_distance_type_t::cosine);

    if(field_json.count(fields::num_dim) == 0) {
        field_json[fields::num_dim] = 0;
        field_json[fields::vec_dist] = DEFAULT_VEC_DIST_METRIC;
    } else {
        if(!field_json[fields::num_dim].is_number_unsigned() || field_json[fields::num_dim] == 0) {
            return Option<bool>(400, "Property `" + fields::num_dim + "` must be a positive integer.");
        }

        if(field_json[fields::type] != field_types::FLOAT_ARRAY) {
            return Option<bool>(400, "Property `" + fields::num_dim + "` is only allowed on a float array field.");
        }

        if(field_json[fields::facet].get<bool>()) {
            return Option<bool>(400, "Property `" + fields::facet + "` is not allowed on a vector field.");
        }

        if(field_json[fields::sort].get<bool>()) {
            return Option<bool>(400, "Property `" + fields::sort + "` cannot be enabled on a vector field.");
        }

        if(field_json.count(fields::vec_dist) == 0) {
            field_json[fields::vec_dist] = DEFAULT_VEC_DIST_METRIC;
        } else {
            if(!field_json[fields::vec_dist].is_string()) {
                return Option<bool>(400, "Property `" + fields::vec_dist + "` must be a string.");
            }

            auto vec_dist_op = magic_enum::enum_cast<vector_distance_type_t>(field_json[fields::vec_dist].get<std::string>());
            if(!vec_dist_op.has_value()) {
                return Option<bool>(400, "Property `" + fields::vec_dist + "` is invalid.");
            }
        }
    }


    if(field_json.count(fields::optional) == 0) {
        // dynamic type fields are always optional
        bool is_dynamic = field::is_dynamic(field_json[fields::name], field_json[fields::type]);
        field_json[fields::optional] = is_dynamic;
    }

    bool is_obj = field_json[fields::type] == field_types::OBJECT || field_json[fields::type] == field_types::OBJECT_ARRAY;
    bool is_regexp_name = field_json[fields::name].get<std::string>().find(".*") != std::string::npos;

    if (is_regexp_name && !field_json[fields::reference].get<std::string>().empty()) {
        return Option<bool>(400, "Wildcard field cannot have a reference.");
    }

    if(is_obj || (!is_regexp_name && enable_nested_fields &&
                   field_json[fields::name].get<std::string>().find('.') != std::string::npos)) {
        field_json[fields::nested] = true;
        field_json[fields::nested_array] = field::VAL_UNKNOWN;  // unknown, will be resolved during read
    } else {
        field_json[fields::nested] = false;
        field_json[fields::nested_array] = 0;
    }

    if(field_json[fields::type] == field_types::GEOPOINT && field_json[fields::sort] == false) {
        LOG(WARNING) << "Forcing geopoint field `" << field_json[fields::name].get<std::string>() << "` to be sortable.";
        field_json[fields::sort] = true;
    }

    auto vec_dist = magic_enum::enum_cast<vector_distance_type_t>(field_json[fields::vec_dist].get<std::string>()).value();

    if (!field_json[fields::reference].get<std::string>().empty()) {
        std::vector<std::string> tokens;
        StringUtils::split(field_json[fields::reference].get<std::string>(), tokens, ".");

        if (tokens.size() < 2) {
            return Option<bool>(400, "Invalid reference `" + field_json[fields::reference].get<std::string>()  + "`.");
        }
    }

    the_fields.emplace_back(
            field(field_json[fields::name], field_json[fields::type], field_json[fields::facet],
                  field_json[fields::optional], field_json[fields::index], field_json[fields::locale],
                  field_json[fields::sort], field_json[fields::infix], field_json[fields::nested],
                  field_json[fields::nested_array], field_json[fields::num_dim], vec_dist,
                  field_json[fields::reference], field_json[fields::embed])
    );

    if (!field_json[fields::reference].get<std::string>().empty()) {
        // Add a reference helper field in the schema. It stores the doc id of the document it references to reduce the
        // computation while searching.
        the_fields.emplace_back(
                field(field_json[fields::name].get<std::string>() + Collection::REFERENCE_HELPER_FIELD_SUFFIX,
                      "int64", false, field_json[fields::optional], true)
        );
    }

    return Option<bool>(true);
}

bool field::flatten_obj(nlohmann::json& doc, nlohmann::json& value, bool has_array, bool has_obj_array,
                        const field& the_field, const std::string& flat_name,
                        const std::unordered_map<std::string, field>& dyn_fields,
                        std::unordered_map<std::string, field>& flattened_fields) {
    if(value.is_object()) {
        has_obj_array = has_array;
        for(const auto& kv: value.items()) {
            flatten_obj(doc, kv.value(), has_array, has_obj_array, the_field, flat_name + "." + kv.key(),
                        dyn_fields, flattened_fields);
        }
    } else if(value.is_array()) {
        for(const auto& kv: value.items()) {
            flatten_obj(doc, kv.value(), true, has_obj_array, the_field, flat_name, dyn_fields, flattened_fields);
        }
    } else { // must be a primitive
        if(doc.count(flat_name) != 0 && flattened_fields.find(flat_name) == flattened_fields.end()) {
            return true;
        }

        std::string detected_type;
        bool found_dynamic_field = false;

        for(auto dyn_field_it = dyn_fields.begin(); dyn_field_it != dyn_fields.end(); dyn_field_it++) {
            auto& dynamic_field = dyn_field_it->second;

            if(dynamic_field.is_auto() || dynamic_field.is_string_star()) {
                continue;
            }

            if(std::regex_match(flat_name, std::regex(flat_name))) {
                detected_type = dynamic_field.type;
                found_dynamic_field = true;
                break;
            }
        }

        if(!found_dynamic_field) {
            if(!field::get_type(value, detected_type)) {
                return false;
            }

            if(std::isalnum(detected_type.back()) && has_array) {
                // convert singular type to multi valued type
                detected_type += "[]";
            }
        }

        if(has_array) {
            doc[flat_name].push_back(value);
        } else {
            doc[flat_name] = value;
        }

        field flattened_field = the_field;
        flattened_field.name = flat_name;
        flattened_field.type = detected_type;
        flattened_field.optional = true;
        flattened_field.nested = true;
        flattened_field.nested_array = has_obj_array;
        flattened_field.set_computed_defaults(-1, -1);
        flattened_fields[flat_name] = flattened_field;
    }

    return true;
}

Option<bool> field::flatten_field(nlohmann::json& doc, nlohmann::json& obj, const field& the_field,
                                  std::vector<std::string>& path_parts, size_t path_index,
                                  bool has_array, bool has_obj_array,
                                  const std::unordered_map<std::string, field>& dyn_fields,
                                  std::unordered_map<std::string, field>& flattened_fields) {
    if(path_index == path_parts.size()) {
        // end of path: check if obj matches expected type
        std::string detected_type;
        bool found_dynamic_field = false;

        for(auto dyn_field_it = dyn_fields.begin(); dyn_field_it != dyn_fields.end(); dyn_field_it++) {
            auto& dynamic_field = dyn_field_it->second;

            if(dynamic_field.is_auto() || dynamic_field.is_string_star()) {
                continue;
            }

            if(std::regex_match(the_field.name, std::regex(dynamic_field.name))) {
                detected_type = dynamic_field.type;
                found_dynamic_field = true;
                break;
            }
        }

        if(!found_dynamic_field) {
            if(!field::get_type(obj, detected_type)) {
                if(obj.is_null() && the_field.optional) {
                    // null values are allowed only if field is optional
                    return Option<bool>(true);
                }

                return Option<bool>(400, "Field `" + the_field.name + "` has an incorrect type.");
            }

            if(std::isalnum(detected_type.back()) && has_array) {
                // convert singular type to multi valued type
                detected_type += "[]";
            }
        }

        has_obj_array = has_obj_array || ((detected_type == field_types::OBJECT) && has_array);

        // handle differences in detection of numerical types
        bool is_numericaly_valid = (detected_type != the_field.type) &&
            ( (detected_type == field_types::INT64 &&
                (the_field.type == field_types::INT32 || the_field.type == field_types::FLOAT)) ||

              (detected_type == field_types::INT64_ARRAY &&
                (the_field.type == field_types::INT32_ARRAY || the_field.type == field_types::FLOAT_ARRAY)) ||

              (detected_type == field_types::FLOAT_ARRAY && the_field.type == field_types::GEOPOINT_ARRAY) ||

              (detected_type == field_types::FLOAT_ARRAY && the_field.type == field_types::GEOPOINT && !has_obj_array)
           );

        if(detected_type == the_field.type || is_numericaly_valid) {
            if(the_field.is_object()) {
                flatten_obj(doc, obj, has_array, has_obj_array, the_field, the_field.name, dyn_fields, flattened_fields);
            } else {
                if(doc.count(the_field.name) != 0 && flattened_fields.find(the_field.name) == flattened_fields.end()) {
                    return Option<bool>(true);
                }

                if(has_array) {
                    doc[the_field.name].push_back(obj);
                } else {
                    doc[the_field.name] = obj;
                }

                field flattened_field = the_field;
                flattened_field.type = detected_type;
                flattened_field.nested = (path_index > 1);
                flattened_field.nested_array = has_obj_array;
                flattened_fields[the_field.name] = flattened_field;
            }

            return Option<bool>(true);
        } else {
            if(has_obj_array && !the_field.is_array()) {
                return Option<bool>(400, "Field `" + the_field.name + "` has an incorrect type. "
                                                                      "Hint: field inside an array of objects must be an array type as well.");
            }

            return Option<bool>(400, "Field `" + the_field.name + "` has an incorrect type.");
        }
    }

    const std::string& fragment = path_parts[path_index];
    const auto& it = obj.find(fragment);

    if(it != obj.end()) {
        if(it.value().is_array()) {
            if(it.value().empty()) {
                return Option<bool>(404, "Field `" + the_field.name + "` not found.");
            }

            has_array = true;
            for(auto& ele: it.value()) {
                has_obj_array = has_obj_array || ele.is_object();
                Option<bool> op = flatten_field(doc, ele, the_field, path_parts, path_index + 1, has_array,
                                                has_obj_array, dyn_fields, flattened_fields);
                if(!op.ok()) {
                    return op;
                }
            }
            return Option<bool>(true);
        } else {
            return flatten_field(doc, it.value(), the_field, path_parts, path_index + 1, has_array, has_obj_array,
                                 dyn_fields, flattened_fields);
        }
    } {
        return Option<bool>(404, "Field `" + the_field.name + "` not found.");
    }
}

Option<bool> field::flatten_doc(nlohmann::json& document,
                                const tsl::htrie_map<char, field>& nested_fields,
                                const std::unordered_map<std::string, field>& dyn_fields,
                                bool missing_is_ok, std::vector<field>& flattened_fields) {

    std::unordered_map<std::string, field> flattened_fields_map;

    for(auto& nested_field: nested_fields) {
        std::vector<std::string> field_parts;
        StringUtils::split(nested_field.name, field_parts, ".");

        if(field_parts.size() > 1 && document.count(nested_field.name) != 0) {
            // skip explicitly present nested fields
            continue;
        }

        auto op = flatten_field(document, document, nested_field, field_parts, 0, false, false,
                                dyn_fields, flattened_fields_map);
        if(op.ok()) {
            continue;
        }

        if(op.code() == 404 && (missing_is_ok || nested_field.optional)) {
            continue;
        } else {
            return op;
        }
    }

    document[".flat"] = nlohmann::json::array();
    for(auto& kv: flattened_fields_map) {
        document[".flat"].push_back(kv.second.name);
        flattened_fields.push_back(kv.second);
    }

    return Option<bool>(true);
}

void field::compact_nested_fields(tsl::htrie_map<char, field>& nested_fields) {
    std::vector<std::string> nested_fields_vec;
    for(const auto& f: nested_fields) {
        nested_fields_vec.push_back(f.name);
    }

    for(auto& field_name: nested_fields_vec) {
        nested_fields.erase_prefix(field_name + ".");
    }
}
