/*
 * Copyright (C) 2018 CESNET, https://photonics.cesnet.cz/
 * Copyright (C) 2018 FIT CVUT, https://fit.cvut.cz/
 *
 * Written by Václav Kubernát <kubervac@fit.cvut.cz>
 *
*/

#include <sysrepo-cpp/Session.h>
#include "sysrepo_access.hpp"
#include "yang_schema.hpp"

leaf_data_ leafValueFromVal(const S_Val& value)
{
    using namespace std::string_literals;
    switch (value->type()) {
    case SR_INT32_T:
        return value->data()->get_int32();
    case SR_UINT32_T:
        return value->data()->get_uint32();
    case SR_BOOL_T:
        return value->data()->get_bool();
    case SR_STRING_T:
        return std::string(value->data()->get_string());
    case SR_ENUM_T:
        return std::string(value->data()->get_enum());
    case SR_DECIMAL64_T:
        return value->data()->get_decimal64();
    case SR_CONTAINER_T:
        return "(container)"s;
    case SR_CONTAINER_PRESENCE_T:
        return "(presence container)"s;
    case SR_LIST_T:
        return "(list)"s;
    default: // TODO: implement all types
        return value->val_to_string();
    }
}

struct valFromValue : boost::static_visitor<S_Val> {
    S_Val operator()(const enum_& value) const
    {
        return std::make_shared<Val>(value.m_value.c_str(), SR_ENUM_T);
    }

    S_Val operator()(const std::string& value) const
    {
        return std::make_shared<Val>(value.c_str());
    }

    S_Val operator()(const uint32_t& value) const
    {
        return std::make_shared<Val>(value, SR_UINT32_T);
    }

    S_Val operator()(const int32_t& value) const
    {
        return std::make_shared<Val>(value, SR_INT32_T);
    }

    S_Val operator()(const bool& value) const
    {
        return std::make_shared<Val>(value, SR_BOOL_T);
    }

    S_Val operator()(const double& value) const
    {
        return std::make_shared<Val>(value);
    }
};

SysrepoAccess::~SysrepoAccess() = default;

SysrepoAccess::SysrepoAccess(const std::string& appname)
    : m_connection(new Connection(appname.c_str()))
    , m_schema(new YangSchema())
{
    m_session = std::make_shared<Session>(m_connection);
    m_schema->registerModuleCallback([this](const char* moduleName, const char* revision, const char* submodule) {
        return fetchSchema(moduleName, revision, submodule);
    });

    for (const auto& it : listImplementedSchemas()) {
        m_schema->loadModule(it);
    }
}

std::map<std::string, leaf_data_> SysrepoAccess::getItems(const std::string& path)
{
    using namespace std::string_literals;
    std::map<std::string, leaf_data_> res;

    auto fillMap = [&res](auto items) {
        if (!items)
            return;
        for (unsigned int i = 0; i < items->val_cnt(); i++) {
            res.emplace(items->val(i)->xpath(), leafValueFromVal(items->val(i)));
        }
    };

    if (path == "/") {
        // Sysrepo doesn't have a root node ("/"), so we take all top-level nodes from all schemas
        auto schemas = m_session->list_schemas();
        for (unsigned int i = 0; i < schemas->schema_cnt(); i++) {
            fillMap(m_session->get_items(("/"s + schemas->schema(i)->module_name() + ":*//.").c_str()));
        }
    } else {
        fillMap(m_session->get_items((path + "//.").c_str()));
    }

    return res;
}

void SysrepoAccess::setLeaf(const std::string& path, leaf_data_ value)
{
    m_session->set_item(path.c_str(), boost::apply_visitor(valFromValue(), value));
}

void SysrepoAccess::createPresenceContainer(const std::string& path)
{
    m_session->set_item(path.c_str());
}

void SysrepoAccess::deletePresenceContainer(const std::string& path)
{
    m_session->delete_item(path.c_str());
}

void SysrepoAccess::commitChanges()
{
    m_session->commit();
}

std::string SysrepoAccess::fetchSchema(const char* module, const char* revision, const char* submodule)
{
    auto schema = m_session->get_schema(module, revision, submodule, SR_SCHEMA_YANG); // FIXME: maybe we should use get_submodule_schema for submodules?
    if (schema.empty())
        throw std::runtime_error(std::string("Module ") + module + " not available");

    return schema;
}

std::vector<std::string> SysrepoAccess::listImplementedSchemas()
{
    std::vector<std::string> res;
    auto schemas = m_session->list_schemas();
    for (unsigned int i = 0; i < schemas->schema_cnt(); i++) {
        auto schema = schemas->schema(i);
        if (schema->implemented())
            res.push_back(schema->module_name());
    }
    return res;
}

std::shared_ptr<Schema> SysrepoAccess::schema()
{
    return m_schema;
}