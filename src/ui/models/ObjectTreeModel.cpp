#include "ui/models/ObjectTreeModel.hpp"

#include "ui/common/Icons.hpp"
#include "ui/common/QtText.hpp"

#include <QLocale>

#include <algorithm>
#include <utility>

namespace mydb::ui {

namespace {

using core::model::DbObject;
using core::model::ObjectRef;
using core::model::ObjectType;

constexpr std::array<ObjectType, 7> kGroupOrder{ObjectType::Table,    ObjectType::View,    ObjectType::Procedure,
                                                ObjectType::Function, ObjectType::Trigger, ObjectType::Event,
                                                ObjectType::Sequence};

QString groupTitle(ObjectType type) {
    switch (type) {
    case ObjectType::Table:
        return ObjectTreeModel::tr("Tabelas");
    case ObjectType::View:
        return ObjectTreeModel::tr("Views");
    case ObjectType::Procedure:
        return ObjectTreeModel::tr("Procedures");
    case ObjectType::Function:
        return ObjectTreeModel::tr("Functions");
    case ObjectType::Trigger:
        return ObjectTreeModel::tr("Triggers");
    case ObjectType::Event:
        return ObjectTreeModel::tr("Events");
    case ObjectType::Sequence:
        return ObjectTreeModel::tr("Sequences");
    case ObjectType::Other:
        break;
    }
    return ObjectTreeModel::tr("Outros");
}

QString formatSize(std::int64_t bytes) {
    return bytes < 0 ? QString() : QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}

} // namespace

ObjectTreeModel::ObjectTreeModel(QObject* parent)
    : QAbstractItemModel(parent),
      m_serverIcon(appIcon("server")),
      m_databaseIcon(appIcon("database")),
      m_folderIcon(appIcon("folder")),
      m_favoriteIcon(appIcon("favorite")) {
    for (std::size_t i = 0; i < m_objectIcons.size(); ++i) {
        m_objectIcons.at(i) = objectIcon(static_cast<ObjectType>(i));
    }
}

ObjectTreeModel::~ObjectTreeModel() = default;

void ObjectTreeModel::setFavoriteCheck(FavoriteCheck check) {
    m_isFavorite = std::move(check);
}

void ObjectTreeModel::setGroupByType(bool group) {
    if (group == m_groupByType) {
        return;
    }
    m_groupByType = group;
    for (const auto& session : m_root.children) {
        for (const auto& database : session->children) {
            if (database->state == LoadState::Loaded) {
                rebuildDatabase(*database);
            }
        }
    }
}

void ObjectTreeModel::addSession(const std::string& sessionId, const QString& title, const QColor& color) {
    if (findSession(sessionId) != nullptr) {
        return;
    }
    auto node = std::make_unique<Node>();
    node->kind = Kind::Session;
    node->key = sessionId;
    node->name = title;
    node->color = color;
    node->state = LoadState::Loading; // a lista de bancos chega por setDatabases()
    std::vector<std::unique_ptr<Node>> nodes;
    nodes.push_back(std::move(node));
    insertChildren(m_root, static_cast<int>(m_root.children.size()), std::move(nodes));
}

void ObjectTreeModel::removeSession(const std::string& sessionId) {
    if (const Node* session = findSession(sessionId)) {
        removeChildren(m_root, session->row, session->row);
    }
}

void ObjectTreeModel::setDatabases(const std::string& sessionId, const std::vector<std::string>& names) {
    Node* session = findSession(sessionId);
    if (session == nullptr) {
        return;
    }
    session->state = LoadState::Loaded;
    // Remove os que sumiram, de trás para frente, e insere os novos na posição certa: os bancos que continuam
    // mantêm os objetos carregados e o estado de expansão na view.
    for (int row = static_cast<int>(session->children.size()) - 1; row >= 0; --row) {
        const std::string& key = session->children[static_cast<std::size_t>(row)]->key;
        if (std::ranges::find(names, key) == names.end()) {
            removeChildren(*session, row, row);
        }
    }
    for (std::size_t position = 0; position < names.size(); ++position) {
        const auto& children = session->children;
        if (position < children.size() && children[position]->key == names[position]) {
            continue;
        }
        const auto existing =
            std::ranges::find(children, names[position], [](const auto& child) { return child->key; });
        if (existing != children.end()) {
            continue; // ordem diferente da anterior: mantém onde está
        }
        auto node = std::make_unique<Node>();
        node->kind = Kind::Database;
        node->key = names[position];
        node->name = toQt(names[position]);
        std::vector<std::unique_ptr<Node>> nodes;
        nodes.push_back(std::move(node));
        insertChildren(*session, static_cast<int>(std::min(position, children.size())), std::move(nodes));
    }
}

void ObjectTreeModel::setObjects(const std::string& sessionId, const std::string& database,
                                 std::vector<DbObject> objects) {
    Node* node = findDatabase(sessionId, database);
    if (node == nullptr) {
        return;
    }
    node->state = LoadState::Loaded;
    rebuildDatabase(*node, std::move(objects));
    std::int64_t total = 0;
    bool known = false;
    for (const DbObject& object : node->loaded) {
        if (object.sizeBytes) {
            total += *object.sizeBytes;
            known = true;
        }
    }
    node->sizeBytes = known ? total : -1;
    const QModelIndex sizeIndex = indexOf(node, kSizeColumn);
    Q_EMIT dataChanged(sizeIndex, sizeIndex, {Qt::DisplayRole, kSizeRole});
}

void ObjectTreeModel::setLoadFailed(const std::string& sessionId, const std::string& database) {
    if (Node* node = findDatabase(sessionId, database)) {
        rebuildDatabase(*node, std::vector<DbObject>{});
        node->state = LoadState::NotLoaded;
        const QModelIndex index = indexOf(node);
        Q_EMIT dataChanged(index, index);
    }
}

void ObjectTreeModel::invalidateDatabase(const std::string& sessionId, const std::string& database) {
    Node* node = findDatabase(sessionId, database);
    if (node == nullptr || node->state == LoadState::NotLoaded) {
        return; // carrega quando for expandido
    }
    // Mantém os objetos atuais na tela enquanto a lista nova não chega. Se uma carga já estava a caminho, ela
    // pode ter lido a versão antiga: um pedido novo entra na fila depois da invalidação do cache.
    node->state = LoadState::Loading;
    Q_EMIT objectsRequested(toQt(sessionId), toQt(database));
}

void ObjectTreeModel::favoriteChanged(const std::string& sessionId, const ObjectRef& object) {
    const QModelIndex found = objectIndex(sessionId, object);
    if (found.isValid()) {
        const QModelIndex last = found.siblingAtColumn(kColumnCount - 1);
        Q_EMIT dataChanged(found.siblingAtColumn(kNameColumn), last, {Qt::DecorationRole, kFavoriteRole});
    }
}

QModelIndex ObjectTreeModel::sessionIndex(const std::string& sessionId) const {
    return indexOf(findSession(sessionId));
}

QModelIndex ObjectTreeModel::databaseIndex(const std::string& sessionId, const std::string& database) const {
    return indexOf(findDatabase(sessionId, database));
}

QModelIndex ObjectTreeModel::objectIndex(const std::string& sessionId, const ObjectRef& object) const {
    const Node* database = findDatabase(sessionId, object.database);
    if (database == nullptr) {
        return {};
    }
    const auto matches = [&object](const Node& node) {
        return node.kind == Kind::Object && node.object && node.object->ref == object;
    };
    for (const auto& child : database->children) {
        if (matches(*child)) {
            return indexOf(child.get());
        }
        for (const auto& grandchild : child->children) {
            if (matches(*grandchild)) {
                return indexOf(grandchild.get());
            }
        }
    }
    return {};
}

bool ObjectTreeModel::isLoaded(const std::string& sessionId, const std::string& database) const {
    const Node* node = findDatabase(sessionId, database);
    return node != nullptr && node->state == LoadState::Loaded;
}

std::vector<std::string> ObjectTreeModel::sessionIds() const {
    std::vector<std::string> ids;
    ids.reserve(m_root.children.size());
    for (const auto& session : m_root.children) {
        ids.push_back(session->key);
    }
    return ids;
}

sdk::Selection ObjectTreeModel::selectionAt(const QModelIndex& index) const {
    sdk::Selection selection;
    const Node* node = nodeAt(index);
    if (node == nullptr || node == &m_root) {
        return selection;
    }
    selection.sessionId = sessionOf(node)->key;
    if (const Node* database = databaseOf(node)) {
        selection.database = database->key;
    }
    if (node->kind == Kind::Object && node->object) {
        selection.object = node->object->ref;
    }
    return selection;
}

ObjectTreeModel::Kind ObjectTreeModel::kindAt(const QModelIndex& index) {
    const Node* node = nodeAt(index);
    return node != nullptr ? node->kind : Kind::Session;
}

const QString& ObjectTreeModel::nameAt(const QModelIndex& index) const {
    const Node* node = nodeAt(index);
    return node != nullptr ? node->name : m_root.name;
}

const DbObject* ObjectTreeModel::objectAt(const QModelIndex& index) {
    const Node* node = nodeAt(index);
    return node != nullptr ? node->object : nullptr;
}

QModelIndex ObjectTreeModel::index(int row, int column, const QModelIndex& parent) const {
    const Node* parentNode = parent.isValid() ? nodeAt(parent) : &m_root;
    if (parentNode == nullptr || row < 0 || column < 0 || column >= kColumnCount ||
        std::cmp_greater_equal(row, parentNode->children.size())) {
        return {};
    }
    return createIndex(row, column, parentNode->children[static_cast<std::size_t>(row)].get());
}

QModelIndex ObjectTreeModel::parent(const QModelIndex& child) const {
    const Node* node = nodeAt(child);
    if (node == nullptr || node->parent == nullptr || node->parent == &m_root) {
        return {};
    }
    return indexOf(node->parent);
}

int ObjectTreeModel::rowCount(const QModelIndex& parent) const {
    if (parent.column() > 0) {
        return 0;
    }
    const Node* node = parent.isValid() ? nodeAt(parent) : &m_root;
    return node != nullptr ? static_cast<int>(node->children.size()) : 0;
}

int ObjectTreeModel::columnCount(const QModelIndex& /*parent*/) const {
    return kColumnCount;
}

QVariant ObjectTreeModel::data(const QModelIndex& index, int role) const {
    const Node* node = nodeAt(index);
    if (node == nullptr) {
        return {};
    }
    switch (role) {
    case Qt::DisplayRole:
        return displayData(*node, index.column());
    case Qt::DecorationRole:
        return decoration(*node, index.column());
    case Qt::TextAlignmentRole:
        return index.column() == kSizeColumn ? QVariant(Qt::AlignRight | Qt::AlignVCenter) : QVariant();
    case Qt::BackgroundRole:
        if (node->kind == Kind::Session && node->color.isValid()) {
            QColor tint = node->color;
            constexpr int kTintAlpha = 70;
            tint.setAlpha(kTintAlpha);
            return tint;
        }
        return {};
    case Qt::ToolTipRole:
        if (node->object && !node->object->comment.empty()) {
            return toQt(node->object->comment);
        }
        return {};
    case kKindRole:
        return static_cast<int>(node->kind);
    case kSessionIdRole:
        return toQt(sessionOf(node)->key);
    case kDatabaseRole: {
        const Node* database = databaseOf(node);
        return database != nullptr ? toQt(database->key) : QString();
    }
    case kObjectTypeRole:
        return node->object ? static_cast<int>(node->object->ref.type) : -1;
    case kSizeRole:
        return static_cast<qint64>(node->kind == Kind::Object && node->object && node->object->sizeBytes
                                       ? *node->object->sizeBytes
                                       : node->sizeBytes);
    case kFavoriteRole:
        return isFavorite(*node);
    default:
        return {};
    }
}

QVariant ObjectTreeModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }
    return section == kNameColumn ? tr("Nome") : tr("Tamanho");
}

bool ObjectTreeModel::hasChildren(const QModelIndex& parent) const {
    const Node* node = parent.isValid() ? nodeAt(parent) : &m_root;
    if (node == nullptr || parent.column() > 0) {
        return false;
    }
    if (node->kind == Kind::Database && node->state != LoadState::Loaded) {
        return true; // mostra a seta de expandir antes de carregar
    }
    return !node->children.empty();
}

bool ObjectTreeModel::canFetchMore(const QModelIndex& parent) const {
    const Node* node = nodeAt(parent);
    return node != nullptr && node->kind == Kind::Database && node->state == LoadState::NotLoaded;
}

void ObjectTreeModel::fetchMore(const QModelIndex& parent) {
    Node* node = nodeAt(parent);
    if (node == nullptr || node->kind != Kind::Database || node->state != LoadState::NotLoaded) {
        return;
    }
    node->state = LoadState::Loading;
    Q_EMIT objectsRequested(toQt(sessionOf(node)->key), toQt(node->key));
}

ObjectTreeModel::Node* ObjectTreeModel::nodeAt(const QModelIndex& index) {
    if (!index.isValid()) {
        return nullptr;
    }
    return static_cast<Node*>(index.internalPointer());
}

QModelIndex ObjectTreeModel::indexOf(const Node* node, int column) const {
    if (node == nullptr || node == &m_root) {
        return {};
    }
    return createIndex(node->row, column, const_cast<Node*>(node)); // NOLINT(cppcoreguidelines-pro-type-const-cast)
}

ObjectTreeModel::Node* ObjectTreeModel::findSession(const std::string& sessionId) const {
    const auto found = std::ranges::find(m_root.children, sessionId, [](const auto& child) { return child->key; });
    return found == m_root.children.end() ? nullptr : found->get();
}

ObjectTreeModel::Node* ObjectTreeModel::findDatabase(const std::string& sessionId, const std::string& database) const {
    const Node* session = findSession(sessionId);
    if (session == nullptr) {
        return nullptr;
    }
    const auto found = std::ranges::find(session->children, database, [](const auto& child) { return child->key; });
    return found == session->children.end() ? nullptr : found->get();
}

const ObjectTreeModel::Node* ObjectTreeModel::sessionOf(const Node* node) const {
    while (node->parent != nullptr && node->parent != &m_root) {
        node = node->parent;
    }
    return node;
}

const ObjectTreeModel::Node* ObjectTreeModel::databaseOf(const Node* node) {
    while (node != nullptr && node->kind != Kind::Database) {
        node = node->kind == Kind::Session ? nullptr : node->parent;
    }
    return node;
}

void ObjectTreeModel::insertChildren(Node& parent, int position, std::vector<std::unique_ptr<Node>> nodes) {
    if (nodes.empty()) {
        return;
    }
    const int count = static_cast<int>(nodes.size());
    beginInsertRows(indexOf(&parent), position, position + count - 1);
    for (auto& node : nodes) {
        node->parent = &parent;
    }
    parent.children.insert(parent.children.begin() + position, std::make_move_iterator(nodes.begin()),
                           std::make_move_iterator(nodes.end()));
    for (int row = position; std::cmp_less(row, parent.children.size()); ++row) {
        parent.children[static_cast<std::size_t>(row)]->row = row;
    }
    endInsertRows();
}

void ObjectTreeModel::removeChildren(Node& parent, int first, int last) {
    beginRemoveRows(indexOf(&parent), first, last);
    parent.children.erase(parent.children.begin() + first, parent.children.begin() + last + 1);
    for (int row = first; std::cmp_less(row, parent.children.size()); ++row) {
        parent.children[static_cast<std::size_t>(row)]->row = row;
    }
    endRemoveRows();
}

void ObjectTreeModel::rebuildDatabase(Node& database, std::optional<std::vector<DbObject>> objects) {
    // Os nós apontam para `loaded`: saem da view antes de a lista mudar.
    if (!database.children.empty()) {
        removeChildren(database, 0, static_cast<int>(database.children.size()) - 1);
    }
    if (objects) {
        database.loaded = std::move(*objects);
    }
    const auto objectNode = [](const DbObject& object) {
        auto node = std::make_unique<Node>();
        node->kind = Kind::Object;
        node->key = object.ref.name;
        node->name = toQt(object.ref.name);
        node->object = &object;
        node->state = LoadState::Loaded;
        return node;
    };

    std::vector<std::unique_ptr<Node>> nodes;
    if (!m_groupByType) {
        nodes.reserve(database.loaded.size());
        for (const DbObject& object : database.loaded) {
            nodes.push_back(objectNode(object));
        }
    } else {
        for (const ObjectType type : kGroupOrder) {
            auto group = std::make_unique<Node>();
            group->kind = Kind::Group;
            group->groupType = type;
            group->state = LoadState::Loaded;
            std::int64_t total = 0;
            for (const DbObject& object : database.loaded) {
                if (object.ref.type == type) {
                    auto child = objectNode(object);
                    child->parent = group.get();
                    child->row = static_cast<int>(group->children.size());
                    total += object.sizeBytes.value_or(0);
                    group->children.push_back(std::move(child));
                }
            }
            if (group->children.empty()) {
                continue;
            }
            group->name = groupTitle(type);
            group->key = group->name.toStdString();
            group->sizeBytes = type == ObjectType::Table ? total : -1;
            nodes.push_back(std::move(group));
        }
    }
    insertChildren(database, 0, std::move(nodes));
}

QVariant ObjectTreeModel::displayData(const Node& node, int column) {
    if (column == kSizeColumn) {
        if (node.kind == Kind::Object) {
            return node.object && node.object->sizeBytes ? formatSize(*node.object->sizeBytes) : QString();
        }
        return formatSize(node.sizeBytes);
    }
    if (node.kind == Kind::Group) {
        return QStringLiteral("%1 (%2)").arg(node.name).arg(node.children.size());
    }
    return node.name;
}

QVariant ObjectTreeModel::decoration(const Node& node, int column) const {
    if (column == kSizeColumn) {
        return node.kind == Kind::Object && isFavorite(node) ? m_favoriteIcon : QVariant();
    }
    switch (node.kind) {
    case Kind::Session:
        return m_serverIcon;
    case Kind::Database:
        return m_databaseIcon;
    case Kind::Group:
        return m_folderIcon;
    case Kind::Object:
        return node.object != nullptr ? m_objectIcons.at(static_cast<std::size_t>(node.object->ref.type)) : QVariant();
    }
    return {};
}

bool ObjectTreeModel::isFavorite(const Node& node) const {
    return node.kind == Kind::Object && node.object != nullptr && static_cast<bool>(m_isFavorite) &&
           m_isFavorite(sessionOf(&node)->key, node.object->ref);
}

} // namespace mydb::ui
