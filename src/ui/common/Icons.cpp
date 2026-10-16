#include "ui/common/Icons.hpp"

#include <QApplication>
#include <QBuffer>
#include <QByteArray>
#include <QFile>
#include <QIconEngine>
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QPixmapCache>
#include <QString>

#include <utility>

namespace mydb::ui {

namespace {

/// Desenha um SVG monocromático de resources/icons no tamanho exato pedido, trocando currentColor pela cor do
/// texto da paleta. O SVG é lido pelo plugin de imagens do QtSvg, carregado só quando o primeiro ícone é
/// desenhado (ADR-0012).
class SvgIconEngine final : public QIconEngine {
public:
    explicit SvgIconEngine(QString name) : m_name(std::move(name)) {}

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State state) override {
        const qreal scale = painter->device() != nullptr ? painter->device()->devicePixelRatioF() : 1.0;
        painter->drawPixmap(rect, scaledPixmap(rect.size(), mode, state, scale));
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override {
        return scaledPixmap(size, mode, state, 1.0);
    }

    QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State /*state*/, qreal scale) override {
        const QSize device = size * scale;
        if (device.isEmpty()) {
            return {};
        }
        const QPalette palette = QApplication::palette();
        QColor color = palette.color(QPalette::WindowText);
        if (mode == QIcon::Disabled) {
            color = palette.color(QPalette::Disabled, QPalette::WindowText);
        } else if (mode == QIcon::Selected) {
            color = palette.color(QPalette::HighlightedText);
        }
        const QString key = QStringLiteral("mydb-icon:%1:%2x%3:%4")
                                .arg(m_name)
                                .arg(device.width())
                                .arg(device.height())
                                .arg(color.rgba());
        QPixmap cached;
        if (QPixmapCache::find(key, &cached)) {
            return cached;
        }
        QPixmap result = render(device, color);
        result.setDevicePixelRatio(scale);
        QPixmapCache::insert(key, result);
        return result;
    }

    [[nodiscard]] QIconEngine* clone() const override { return new SvgIconEngine(m_name); }
    [[nodiscard]] QString key() const override { return QStringLiteral("mydb-svg"); }

private:
    [[nodiscard]] QPixmap render(const QSize& size, const QColor& color) const {
        QFile file(QStringLiteral(":/icons/") + m_name + QStringLiteral(".svg"));
        if (!file.open(QIODevice::ReadOnly)) {
            return {};
        }
        QByteArray svg = file.readAll();
        svg.replace("currentColor", color.name(QColor::HexRgb).toLatin1());
        QBuffer buffer(&svg);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer, "svg");
        reader.setScaledSize(size);
        const QImage image = reader.read();
        return image.isNull() ? QPixmap() : QPixmap::fromImage(image);
    }

    QString m_name;
};

} // namespace

QIcon appIcon(std::string_view name) {
    const QString file = QStringLiteral(":/icons/") +
                         QString::fromLatin1(name.data(), static_cast<qsizetype>(name.size())) + QStringLiteral(".svg");
    if (!QFile::exists(file)) {
        return {};
    }
    return QIcon(new SvgIconEngine(QString::fromLatin1(name.data(), static_cast<qsizetype>(name.size()))));
}

QIcon objectIcon(core::model::ObjectType type) {
    using core::model::ObjectType;
    switch (type) {
    case ObjectType::Table:
        return appIcon("table");
    case ObjectType::View:
        return appIcon("view");
    case ObjectType::Procedure:
        return appIcon("procedure");
    case ObjectType::Function:
        return appIcon("function");
    case ObjectType::Trigger:
        return appIcon("trigger");
    case ObjectType::Event:
        return appIcon("event");
    case ObjectType::Sequence:
        return appIcon("sequence");
    case ObjectType::Other:
        break;
    }
    return appIcon("table");
}

} // namespace mydb::ui
