// Copyright Yahoo. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.

#include "resultclass.h"
#include "docsum_field_writer.h"
#include "resultconfig.h"
#include <vespa/vespalib/stllike/hashtable.hpp>
#include <cassert>

namespace search::docsummary {

ResultClass::ResultClass(const char *name, util::StringEnum & fieldEnum)
    : _name(name),
      _entries(),
      _nameMap(),
      _fieldEnum(fieldEnum),
      _enumMap(),
      _dynInfo(),
      _omit_summary_features(false),
      _num_field_writer_states(0)
{ }


ResultClass::~ResultClass() = default;

int
ResultClass::GetIndexFromName(const char* name) const
{
    NameIdMap::const_iterator found(_nameMap.find(name));
    return (found != _nameMap.end()) ? found->second : -1;
}

bool
ResultClass::AddConfigEntry(const char *name, ResType type, std::unique_ptr<DocsumFieldWriter> docsum_field_writer)
{
    if (_nameMap.find(name) != _nameMap.end())
        return false;

    _nameMap[name] = _entries.size();
    ResConfigEntry e;
    e._type      = type;
    e._bindname  = name;
    e._enumValue = _fieldEnum.Add(name);
    assert(e._enumValue >= 0);
    if (docsum_field_writer) {
        docsum_field_writer->setIndex(_entries.size());
        bool generated = docsum_field_writer->IsGenerated();
        getDynamicInfo().update_override_counts(generated);
        if (docsum_field_writer->setFieldWriterStateIndex(_num_field_writer_states)) {
            ++_num_field_writer_states;
        }
    }
    e._docsum_field_writer = std::move(docsum_field_writer);
    _entries.push_back(std::move(e));
    return true;
}

bool
ResultClass::AddConfigEntry(const char *name, ResType type)
{
    return AddConfigEntry(name, type, {});
}

void
ResultClass::CreateEnumMap()
{
    _enumMap.resize(_fieldEnum.GetNumEntries());

    for (int & value : _enumMap) {
        value = -1;
    }
    for (uint32_t i(0); i < _entries.size(); i++) {
        _enumMap[_entries[i]._enumValue] = i;
    }
}

}
