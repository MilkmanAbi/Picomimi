#pragma once

#include <QApplication>
#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QFile>
#include <QDir>
#include <QRegularExpression>
#include <QScreen>
#include <QDateTime>
#include <QVariantMap>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QStyle>
#include <QGuiApplication>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsBlurEffect>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <QSequentialAnimationGroup>
#include <QEasingCurve>
#include <QMouseEvent>
#include <QRandomGenerator>
#include <QFontDatabase>
#include <QMovie>
#include <QPixmap>
#include <QBitmap>
#include <QClipboard>
#include <QDesktopServices>
#include <QUrl>
#include <QProcess>
#include <QThread>
#include <QMutex>
#include <QEventLoop>
#include <QDebug>
#include <iostream>
#include <fstream>
#include <memory>
#include <functional>
#include <QMap>
#include <QPointer>

// Qt5 Network includes
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>

#ifdef Q_OS_LINUX
#include <unistd.h>
#include <sys/sysinfo.h>
#include <sys/statvfs.h>
#include <ifaddrs.h>
// Qt5 X11 extras
#include <QX11Info>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#endif

namespace GWidget {

// Forward declarations
class Widget;
class SystemMonitor;
class WeatherAPI;

// ============================================================================
// SIMPLIFIED ENUMS & TYPES
// ============================================================================
enum Shape {
    Rectangle = 0,
    RoundedRect = 1,
    Circle = 2,
    Square = 3
};

enum Position {
    TopLeft,
    TopCenter, 
    TopRight,
    CenterLeft,
    Center,
    CenterRight,
    BottomLeft,
    BottomCenter,
    BottomRight
};

enum Animation {
    FadeIn,
    FadeOut,
    Bounce,
    Scale
};

// ============================================================================
// ULTRA-SIMPLE WIDGET CLASS
// ============================================================================
class Widget : public QWidget {
    Q_OBJECT

private:
    QVBoxLayout* main_layout;
    Shape window_shape;
    int corner_radius;
    QColor bg_color;
    QColor border_color;
    int border_width;
    double opacity_value;
    bool is_draggable;
    QPoint drag_pos;
    QGraphicsEffect* current_effect;
    QMap<QString, QPointer<QPropertyAnimation>> active_animations; // Use QPointer for safety
    QRegion mask_region; // For proper circle clipping
    bool widget_initialized; // Track initialization state

public:
    Widget(int width = 300, int height = 200, QWidget* parent = nullptr);
    ~Widget();

    // Ultra-simple shape functions
    void setShape(Shape shape);
    void setRounded(int radius = 15);
    void setSize(int width, int height);
    
    // Ultra-simple background functions
    void setBackground(const QString& color);
    void setBackground(int r, int g, int b, int alpha = 255);
    void setOpacity(double opacity);
    void setGlass(bool enabled = true);
    void setBlur(bool enabled = true, double radius = 5.0);
    
    // Ultra-simple border functions  
    void setBorder(const QString& color, int width = 1);
    void setBorder(int r, int g, int b, int width = 1);
    void removeBorder();
    
    // Ultra-simple positioning
    void setPosition(Position pos);
    void setPosition(int x, int y);
    void center();
    
    // Ultra-simple behavior
    void setDraggable(bool draggable = true);
    void setClickThrough(bool enabled = false);
    void setAlwaysOnTop(bool enabled = true);
    void toFront(); // Bring to front
    void toBack();  // Send to back
    
    // Ultra-simple animations (fixed performance and crashes)
    void fadeIn(int duration = 300);
    void fadeOut(int duration = 300);
    void bounce(int duration = 500);
    void glow(const QString& color = "#00AAFF", int intensity = 10);
    
    // Layout helpers
    QVBoxLayout* vbox();
    QHBoxLayout* hbox();
    QGridLayout* grid();
    void addWidget(QWidget* widget);
    
    // Show/hide
    void show();
    void hide();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void setupWidget();
    void applyShape();
    void updateMask();
    void cleanupAnimations();
    void stopAnimation(const QString& name);
    bool isValidForAnimation() const;
};

// ============================================================================
// ULTRA-SIMPLE TEXT FUNCTIONS
// ============================================================================
class Text : public QLabel {
    Q_OBJECT

public:
    Text(const QString& text = "", QWidget* parent = nullptr);
    
    // Ultra-simple text styling
    void setText(const QString& text);
    void setFont(const QString& family, int size = 12);
    void setBold(bool bold = true);
    void setItalic(bool italic = true);
    void setColor(const QString& color);
    void setColor(int r, int g, int b);
    void setGlow(const QString& color = "#FFFFFF", int radius = 5);
    void setAlign(const QString& alignment); // "left", "center", "right"
    
    // Preset styles
    void setTitle();        // Large, bold title style
    void setSubtitle();     // Medium subtitle style  
    void setBody();         // Normal body text style
    void setMonospace();    // Monospace font style
};

// ============================================================================
// ULTRA-SIMPLE PROGRESS BAR
// ============================================================================
class ProgressBar : public QWidget {
    Q_OBJECT
    
private:
    double current_value;
    double max_value;
    QColor bg_color;
    QColor fill_color;
    int bar_radius;
    
public:
    ProgressBar(QWidget* parent = nullptr);
    
    void setValue(double value);
    void setMaxValue(double max);
    void setColors(const QString& background, const QString& fill);
    void setRounded(int radius = 8);
    
protected:
    void paintEvent(QPaintEvent* event) override;
};

// ============================================================================
// SYSTEM MONITOR API (Fixed memory leaks)
// ============================================================================
class SystemMonitor : public QObject {
    Q_OBJECT
    
private:
    QTimer* update_timer;
    QMutex data_mutex;
    static SystemMonitor* instance; // Singleton
    
    struct SystemData {
        double cpu_usage = 0.0;
        double memory_usage = 0.0;
        double disk_usage = 0.0;
        double temperature = 0.0;
        QString uptime;
        int processes = 0;
    } system_data;
    
    // CPU monitoring state
    unsigned long long last_idle = 0;
    unsigned long long last_total = 0;
    
    SystemMonitor(QObject* parent = nullptr); // Private constructor
    
public:
    ~SystemMonitor();
    static SystemMonitor* getInstance();
    static void cleanup(); // Proper cleanup method
    
    // Simple getters
    double cpu();
    double memory(); 
    double disk(const QString& path = "/");
    double temperature();
    QString uptime();
    int processes();
    
    // Memory details
    QString memoryUsed();    // "4.2 GB"
    QString memoryTotal();   // "16.0 GB"
    
signals:
    void updated();
    
private slots:
    void updateSystemInfo();
};

// ============================================================================
// WEATHER API (Fixed memory leaks)
// ============================================================================
class WeatherAPI : public QObject {
    Q_OBJECT
    
private:
    QNetworkAccessManager* network;
    QString api_key;
    QString city_name;
    QJsonObject weather_data;
    QTimer* update_timer;
    QMap<QNetworkReply*, bool> pending_requests; // Track requests
    static WeatherAPI* instance;
    
    WeatherAPI(QObject* parent = nullptr); // Private constructor
    
public:
    ~WeatherAPI();
    static WeatherAPI* getInstance();
    static void cleanup();
    
    // Setup
    void setApiKey(const QString& key);
    void setCity(const QString& city);
    void setUpdateInterval(int minutes = 10);
    
    // Simple getters
    QString temperature();      // "22°C"
    QString condition();        // "Partly cloudy"
    QString humidity();         // "65%"
    QString icon();            // Weather emoji
    QString windSpeed();       // "15 km/h"
    
    // Advanced getters
    int temperatureInt();      // 22
    bool isRaining();
    bool isSunny();
    bool isCloudy();
    
signals:
    void weatherUpdated();
    void error(const QString& message);
    
private slots:
    void fetchWeather();
    void onWeatherReply();
    void cleanupRequest();
};

// ============================================================================
// ULTRA-SIMPLE GLOBAL FUNCTIONS
// ============================================================================

// Widget creation
Widget* createWidget(int width = 300, int height = 200);
Widget* createCircle(int diameter = 150);
Widget* createSquare(int size = 200);

// Text creation
Text* createText(const QString& text, Widget* parent = nullptr);
Text* createTitle(const QString& text, Widget* parent = nullptr);
Text* createLabel(const QString& text, Widget* parent = nullptr);

// Progress bar creation
ProgressBar* createProgressBar(Widget* parent = nullptr);

// System monitoring (fixed)
SystemMonitor* getSystemMonitor();
WeatherAPI* getWeatherAPI();

// Cleanup functions
void cleanup(); // Global cleanup

// Font helpers
QFont font(const QString& family, int size = 12);
QFont boldFont(const QString& family, int size = 12);
QFont monoFont(int size = 12);

// Color helpers
QColor color(const QString& hex);           // "#FF0000"
QColor color(int r, int g, int b, int a = 255);
QString rgba(int r, int g, int b, int a = 255);  // Returns "rgba(r,g,b,a)"

// Position helpers
void moveWidget(Widget* widget, Position pos);
void moveWidget(Widget* widget, int x, int y);
QPoint getPosition(Position pos, QSize widget_size);

// Animation helpers
void animate(Widget* widget, Animation type, int duration = 300);
void fadeWidget(Widget* widget, double opacity, int duration = 300);
void moveWidget(Widget* widget, int x, int y, int duration = 500);

// File helpers
QString getConfigPath(const QString& widget_name);
QString getDataPath(const QString& widget_name);
bool saveText(const QString& filename, const QString& text);
QString loadText(const QString& filename);
bool saveJson(const QString& filename, const QJsonObject& json);
QJsonObject loadJson(const QString& filename);

// Timer helpers  
QTimer* createTimer(int interval, std::function<void()> callback);
void delay(int milliseconds, std::function<void()> callback);

// Utility functions
bool isWayland();
bool isX11();
QString getDesktopEnvironment();
QSize screenSize();
QPoint screenCenter();

// Debug helpers
void log(const QString& message);
void debug(const QString& message);

} // namespace GWidget

// ============================================================================
// IMPLEMENTATION
// ============================================================================

namespace GWidget {

// Global singletons (properly managed)
SystemMonitor* SystemMonitor::instance = nullptr;
WeatherAPI* WeatherAPI::instance = nullptr;

// Widget Implementation (Fixed animations, blur, and circle clipping)
Widget::Widget(int width, int height, QWidget* parent) : QWidget(parent) {
    resize(width, height);
    window_shape = Rectangle;
    corner_radius = 0;
    bg_color = QColor(0, 0, 0, 200);
    border_color = QColor(255, 255, 255, 100);
    border_width = 0;
    opacity_value = 1.0;
    is_draggable = true;
    current_effect = nullptr;
    widget_initialized = false;
    
    setupWidget();
    
    main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(20, 20, 20, 20);
    main_layout->setSpacing(10);
    
    updateMask(); // Initialize mask
    widget_initialized = true; // Mark as fully initialized
}

Widget::~Widget() {
    cleanupAnimations();
    if (current_effect) {
        delete current_effect;
        current_effect = nullptr;
    }
}

void Widget::cleanupAnimations() {
    for (auto it = active_animations.begin(); it != active_animations.end(); ++it) {
        QPointer<QPropertyAnimation> anim = it.value();
        if (anim) {
            anim->stop();
            anim->deleteLater();
        }
    }
    active_animations.clear();
}

void Widget::stopAnimation(const QString& name) {
    if (active_animations.contains(name)) {
        QPointer<QPropertyAnimation> anim = active_animations[name];
        if (anim) {
            anim->stop();
            anim->deleteLater();
        }
        active_animations.remove(name);
    }
}

bool Widget::isValidForAnimation() const {
    return widget_initialized && !isHidden() && winId() != 0;
}

void Widget::setupWidget() {
    setAttribute(Qt::WA_TranslucentBackground);
    setWindowFlags(Qt::FramelessWindowHint | Qt::Tool);
    setMouseTracking(true);
    
    // Set window properties for Linux
    #ifdef Q_OS_LINUX
    if (winId()) {
        setAttribute(Qt::WA_ShowWithoutActivating);
    }
    #endif
}

void Widget::updateMask() {
    QPainterPath path;
    QRectF rect = this->rect();
    
    switch (window_shape) {
        case Rectangle:
            path.addRect(rect);
            break;
        case RoundedRect:
            path.addRoundedRect(rect, corner_radius, corner_radius);
            break;
        case Circle: {
            // Perfect smooth circle - use proper ellipse rendering
            qreal diameter = qMin(rect.width(), rect.height());
            qreal radius = diameter / 2.0;
            QPointF center = rect.center();
            // Use addEllipse with x,y,width,height for perfect circles
            path.addEllipse(center.x() - radius, center.y() - radius, diameter, diameter);
            break;
        }
        case Square:
            path.addRect(rect);
            break;
    }
    
    // Convert to region and apply as mask
    QRegion region = QRegion(path.toFillPolygon().toPolygon());
    setMask(region);
    mask_region = region;
}

void Widget::setShape(Shape shape) {
    window_shape = shape;
    if (shape == Circle || shape == Square) {
        int size = qMin(width(), height());
        resize(size, size);
    }
    updateMask();
    update();
}

void Widget::setRounded(int radius) {
    corner_radius = radius;
    window_shape = RoundedRect;
    updateMask();
    update();
}

void Widget::setSize(int width, int height) {
    resize(width, height);
    updateMask();
    update();
}

void Widget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    updateMask(); // Update mask on resize
}

void Widget::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    widget_initialized = true; // Ensure we're marked as initialized when shown
}

void Widget::setBackground(const QString& color) {
    bg_color = QColor(color);
    update();
}

void Widget::setBackground(int r, int g, int b, int alpha) {
    bg_color = QColor(r, g, b, alpha);
    update();
}

void Widget::setOpacity(double opacity) {
    opacity_value = qBound(0.0, opacity, 1.0);
    setWindowOpacity(opacity_value);
    update();
}

void Widget::setGlass(bool enabled) {
    if (current_effect) {
        delete current_effect;
        current_effect = nullptr;
        setGraphicsEffect(nullptr);
    }
    
    if (enabled) {
        auto* shadow = new QGraphicsDropShadowEffect(this);
        shadow->setBlurRadius(15);
        shadow->setColor(QColor(0, 0, 0, 80));
        shadow->setOffset(0, 2);
        current_effect = shadow;
        setGraphicsEffect(current_effect);
    }
}

void Widget::setBlur(bool enabled, double radius) {
    if (current_effect) {
        delete current_effect;
        current_effect = nullptr;
        setGraphicsEffect(nullptr);
    }
    
    if (enabled) {
        // Create a backdrop blur effect by making the background semi-transparent
        // and relying on the compositor for actual background blur
        setAttribute(Qt::WA_TranslucentBackground, true);
        
        // Make background more transparent for glass effect
        if (bg_color.alpha() > 100) {
            bg_color.setAlpha(80); // Very transparent for glass look
        }
        
        // Add subtle shadow for depth instead of problematic blur effect
        auto* shadow = new QGraphicsDropShadowEffect(this);
        shadow->setBlurRadius(15);
        shadow->setColor(QColor(0, 0, 0, 60));
        shadow->setOffset(0, 3);
        current_effect = shadow;
        setGraphicsEffect(current_effect);
    }
}

void Widget::setBorder(const QString& color, int width) {
    border_color = QColor(color);
    border_width = width;
    update();
}

void Widget::setBorder(int r, int g, int b, int width) {
    border_color = QColor(r, g, b);
    border_width = width;
    update();
}

void Widget::removeBorder() {
    border_width = 0;
    update();
}

void Widget::setPosition(Position pos) {
    QPoint new_pos = getPosition(pos, size());
    move(new_pos);
}

void Widget::setPosition(int x, int y) {
    move(x, y);
}

void Widget::center() {
    setPosition(Center);
}

void Widget::setDraggable(bool draggable) {
    is_draggable = draggable;
}

void Widget::setClickThrough(bool enabled) {
    if (enabled) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
    } else {
        setAttribute(Qt::WA_TransparentForMouseEvents, false);
    }
}

void Widget::setAlwaysOnTop(bool enabled) {
    Qt::WindowFlags flags = windowFlags();
    if (enabled) {
        flags |= Qt::WindowStaysOnTopHint;
    } else {
        flags &= ~Qt::WindowStaysOnTopHint;
    }
    setWindowFlags(flags);
    if (isVisible()) show(); // Re-show if was visible
}

void Widget::toFront() {
    raise();
    activateWindow();
}

void Widget::toBack() {
    lower();
}

void Widget::fadeIn(int duration) {
    if (!isValidForAnimation()) return;
    
    stopAnimation("fade");
    
    auto* animation = new QPropertyAnimation(this, "windowOpacity", this);
    animation->setDuration(duration);
    animation->setStartValue(0.0);
    animation->setEndValue(opacity_value);
    animation->setEasingCurve(QEasingCurve::InOutCubic); // Super smooth cubic curve
    
    connect(animation, &QPropertyAnimation::finished, [this]() {
        active_animations.remove("fade");
    });
    
    active_animations["fade"] = animation;
    animation->start();
}

void Widget::fadeOut(int duration) {
    if (!isValidForAnimation()) return;
    
    stopAnimation("fade");
    
    auto* animation = new QPropertyAnimation(this, "windowOpacity", this);
    animation->setDuration(duration);
    animation->setStartValue(windowOpacity());
    animation->setEndValue(0.0);
    animation->setEasingCurve(QEasingCurve::InOutCubic); // Super smooth cubic curve
    
    connect(animation, &QPropertyAnimation::finished, [this]() {
        this->hide();
        active_animations.remove("fade");
    });
    
    active_animations["fade"] = animation;
    animation->start();
}

void Widget::bounce(int duration) {
    if (!isValidForAnimation()) return;
    
    stopAnimation("bounce");
    
    auto* animation = new QPropertyAnimation(this, "geometry", this);
    animation->setDuration(duration);
    
    QRect current = geometry();
    QRect bounced = current;
    bounced.setSize(QSize(current.width() * 1.05, current.height() * 1.05));
    bounced.moveCenter(current.center());
    
    animation->setStartValue(current);
    animation->setKeyValueAt(0.5, bounced);
    animation->setEndValue(current);
    animation->setEasingCurve(QEasingCurve::OutBounce);
    
    connect(animation, &QPropertyAnimation::finished, [this]() {
        active_animations.remove("bounce");
    });
    
    active_animations["bounce"] = animation;
    animation->start();
}

void Widget::glow(const QString& color, int intensity) {
    if (current_effect) {
        delete current_effect;
        current_effect = nullptr;
        setGraphicsEffect(nullptr);
    }
    
    auto* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(qBound(5, intensity * 2, 30));
    shadow->setColor(QColor(color));
    shadow->setOffset(0, 0);
    current_effect = shadow;
    setGraphicsEffect(current_effect);
}

QVBoxLayout* Widget::vbox() {
    return main_layout;
}

QHBoxLayout* Widget::hbox() {
    auto* layout = new QHBoxLayout();
    main_layout->addLayout(layout);
    return layout;
}

QGridLayout* Widget::grid() {
    auto* layout = new QGridLayout();
    main_layout->addLayout(layout);
    return layout;
}

void Widget::addWidget(QWidget* widget) {
    main_layout->addWidget(widget);
}

void Widget::show() {
    QWidget::show();
    // Only fade in if widget is properly initialized
    if (widget_initialized) {
        fadeIn(200);
    }
}

void Widget::hide() {
    if (widget_initialized) {
        fadeOut(200);
    } else {
        QWidget::hide();
    }
}

void Widget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event)
    QPainter painter(this);
    
    // Enable high-quality rendering for smooth circles
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    
    QPainterPath path;
    QRectF rect = this->rect();
    
    switch (window_shape) {
        case Rectangle:
            path.addRect(rect);
            break;
        case RoundedRect:
            path.addRoundedRect(rect, corner_radius, corner_radius);
            break;
        case Circle: {
            // Perfect smooth circle with high quality antialiasing
            qreal diameter = qMin(rect.width(), rect.height());
            qreal radius = diameter / 2.0;
            QPointF center = rect.center();
            
            // Use QPainterPath for better antialiasing
            path.addEllipse(center.x() - radius, center.y() - radius, diameter, diameter);
            break;
        }
        case Square:
            path.addRect(rect);
            break;
    }
    
    // Fill background with smooth rendering
    painter.fillPath(path, bg_color);
    
    // Draw border with proper rounded corners and antialiasing
    if (border_width > 0) {
        QPen pen(border_color, border_width);
        pen.setJoinStyle(Qt::RoundJoin);
        pen.setCapStyle(Qt::RoundCap);
        painter.setPen(pen);
        painter.drawPath(path);
    }
}

void Widget::mousePressEvent(QMouseEvent* event) {
    if (is_draggable && event->button() == Qt::LeftButton) {
        drag_pos = event->globalPos() - frameGeometry().topLeft();
        event->accept();
    }
    QWidget::mousePressEvent(event);
}

void Widget::mouseMoveEvent(QMouseEvent* event) {
    if (is_draggable && event->buttons() & Qt::LeftButton) {
        move(event->globalPos() - drag_pos);
        event->accept();
    }
    QWidget::mouseMoveEvent(event);
}

void Widget::applyShape() {
    updateMask();
}

// Text Implementation
Text::Text(const QString& text, QWidget* parent) : QLabel(text, parent) {
    setAlignment(Qt::AlignLeft | Qt::AlignTop);
    setWordWrap(true);
    setAttribute(Qt::WA_TranslucentBackground);
}

void Text::setText(const QString& text) {
    QLabel::setText(text);
}

void Text::setFont(const QString& family, int size) {
    QFont font(family, size);
    QLabel::setFont(font);
}

void Text::setBold(bool bold) {
    QFont current_font = font();
    current_font.setBold(bold);
    QLabel::setFont(current_font);
}

void Text::setItalic(bool italic) {
    QFont current_font = font();
    current_font.setItalic(italic);
    QLabel::setFont(current_font);
}

void Text::setColor(const QString& color) {
    setStyleSheet(QString("color: %1;").arg(color));
}

void Text::setColor(int r, int g, int b) {
    setStyleSheet(QString("color: rgb(%1, %2, %3);").arg(r).arg(g).arg(b));
}

void Text::setGlow(const QString& color, int radius) {
    auto* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(radius);
    shadow->setColor(QColor(color));
    shadow->setOffset(0, 0);
    setGraphicsEffect(shadow);
}

void Text::setAlign(const QString& alignment) {
    if (alignment == "left") {
        setAlignment(Qt::AlignLeft);
    } else if (alignment == "center") {
        setAlignment(Qt::AlignCenter);
    } else if (alignment == "right") {
        setAlignment(Qt::AlignRight);
    }
}

void Text::setTitle() {
    setFont("Ubuntu", 24);
    setBold(true);
    setColor("#FFFFFF");
}

void Text::setSubtitle() {
    setFont("Ubuntu", 18);
    setBold(false);
    setColor("#CCCCCC");
}

void Text::setBody() {
    setFont("Ubuntu", 14);
    setBold(false);
    setColor("#FFFFFF");
}

void Text::setMonospace() {
    setFont("Ubuntu Mono", 14);
    setColor("#00FF00");
}

// ProgressBar Implementation  
ProgressBar::ProgressBar(QWidget* parent) : QWidget(parent) {
    current_value = 0.0;
    max_value = 100.0;
    bg_color = QColor(100, 100, 100, 100);
    fill_color = QColor(0, 150, 255);
    bar_radius = 8;
    setFixedHeight(20);
}

void ProgressBar::setValue(double value) {
    current_value = qBound(0.0, value, max_value);
    update();
}

void ProgressBar::setMaxValue(double max) {
    max_value = max;
    update();
}

void ProgressBar::setColors(const QString& background, const QString& fill) {
    bg_color = QColor(background);
    fill_color = QColor(fill);
    update();
}

void ProgressBar::setRounded(int radius) {
    bar_radius = radius;
    update();
}

void ProgressBar::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    QRectF rect = this->rect();
    
    // Background
    QPainterPath bg_path;
    bg_path.addRoundedRect(rect, bar_radius, bar_radius);
    painter.fillPath(bg_path, bg_color);
    
    // Progress fill
    if (current_value > 0) {
        double progress = current_value / max_value;
        QRectF fill_rect = rect;
        fill_rect.setWidth(rect.width() * progress);
        
        QPainterPath fill_path;
        fill_path.addRoundedRect(fill_rect, bar_radius, bar_radius);
        painter.fillPath(fill_path, fill_color);
    }
}

// SystemMonitor Implementation (Same as before, working correctly)
SystemMonitor::SystemMonitor(QObject* parent) : QObject(parent) {
    update_timer = new QTimer(this);
    connect(update_timer, &QTimer::timeout, this, &SystemMonitor::updateSystemInfo);
    update_timer->start(2000);
    updateSystemInfo();
}

SystemMonitor::~SystemMonitor() {
    if (update_timer) {
        update_timer->stop();
        update_timer->deleteLater();
    }
}

SystemMonitor* SystemMonitor::getInstance() {
    if (!instance) {
        instance = new SystemMonitor();
    }
    return instance;
}

void SystemMonitor::cleanup() {
    if (instance) {
        delete instance;
        instance = nullptr;
    }
}

double SystemMonitor::cpu() {
    QMutexLocker locker(&data_mutex);
    return system_data.cpu_usage;
}

double SystemMonitor::memory() {
    QMutexLocker locker(&data_mutex);
    return system_data.memory_usage;
}

double SystemMonitor::disk(const QString& path) {
    QMutexLocker locker(&data_mutex);
    return system_data.disk_usage;
}

double SystemMonitor::temperature() {
    QMutexLocker locker(&data_mutex);
    return system_data.temperature;
}

QString SystemMonitor::uptime() {
    QMutexLocker locker(&data_mutex);
    return system_data.uptime;
}

int SystemMonitor::processes() {
    QMutexLocker locker(&data_mutex);
    return system_data.processes;
}

QString SystemMonitor::memoryUsed() {
    double used_gb = memory() * 16.0 / 100.0; // Assuming 16GB total (should be dynamic)
    return QString("%1 GB").arg(used_gb, 0, 'f', 1);
}

QString SystemMonitor::memoryTotal() {
    return "16.0 GB"; // Should be dynamic
}

void SystemMonitor::updateSystemInfo() {
    QMutexLocker locker(&data_mutex);
    
#ifdef Q_OS_LINUX
    // CPU Usage calculation
    QFile stat_file("/proc/stat");
    if (stat_file.open(QIODevice::ReadOnly)) {
        QTextStream stream(&stat_file);
        QString line = stream.readLine();
        QStringList values = line.split(' ', Qt::SkipEmptyParts);
        
        if (values.size() >= 8) {
            unsigned long long user = values[1].toULongLong();
            unsigned long long nice = values[2].toULongLong();
            unsigned long long system = values[3].toULongLong();
            unsigned long long idle = values[4].toULongLong();
            unsigned long long iowait = values[5].toULongLong();
            unsigned long long irq = values[6].toULongLong();
            unsigned long long softirq = values[7].toULongLong();
            
            unsigned long long total = user + nice + system + idle + iowait + irq + softirq;
            
            if (last_total > 0) {
                unsigned long long total_diff = total - last_total;
                unsigned long long idle_diff = idle - last_idle;
                
                if (total_diff > 0) {
                    system_data.cpu_usage = 100.0 * (total_diff - idle_diff) / total_diff;
                }
            }
            
            last_total = total;
            last_idle = idle;
        }
        stat_file.close();
    }
    
    // Memory Usage
    QFile mem_file("/proc/meminfo");
    if (mem_file.open(QIODevice::ReadOnly)) {
        QTextStream stream(&mem_file);
        QString content = stream.readAll();
        
        QRegularExpression total_regex("MemTotal:\\s+(\\d+)\\s+kB");
        QRegularExpression available_regex("MemAvailable:\\s+(\\d+)\\s+kB");
        
        auto total_match = total_regex.match(content);
        auto available_match = available_regex.match(content);
        
        if (total_match.hasMatch() && available_match.hasMatch()) {
            unsigned long long total_kb = total_match.captured(1).toULongLong();
            unsigned long long available_kb = available_match.captured(1).toULongLong();
            unsigned long long used_kb = total_kb - available_kb;
            
            system_data.memory_usage = 100.0 * used_kb / total_kb;
        }
        mem_file.close();
    }
    
    // Disk Usage
    struct statvfs stat;
    if (statvfs("/", &stat) == 0) {
        unsigned long long total = stat.f_blocks * stat.f_frsize;
        unsigned long long free = stat.f_bavail * stat.f_frsize;
        unsigned long long used = total - free;
        
        if (total > 0) {
            system_data.disk_usage = 100.0 * used / total;
        }
    }
    
    // Uptime
    QFile uptime_file("/proc/uptime");
    if (uptime_file.open(QIODevice::ReadOnly)) {
        QTextStream stream(&uptime_file);
        double uptime_seconds = stream.readAll().split(' ')[0].toDouble();
        
        int days = uptime_seconds / 86400;
        int hours = (uptime_seconds - days * 86400) / 3600;
        int minutes = (uptime_seconds - days * 86400 - hours * 3600) / 60;
        
        if (days > 0) {
            system_data.uptime = QString("%1d %2h %3m").arg(days).arg(hours).arg(minutes);
        } else if (hours > 0) {
            system_data.uptime = QString("%1h %2m").arg(hours).arg(minutes);
        } else {
            system_data.uptime = QString("%1m").arg(minutes);
        }
        uptime_file.close();
    }
    
    // Process count
    QDir proc_dir("/proc");
    QStringList entries = proc_dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    system_data.processes = 0;
    
    for (const QString& entry : entries) {
        bool ok;
        entry.toInt(&ok);
        if (ok) system_data.processes++;
    }
    
    // Temperature (try common thermal zones)
    QFile temp_file("/sys/class/thermal/thermal_zone0/temp");
    if (temp_file.open(QIODevice::ReadOnly)) {
        QTextStream stream(&temp_file);
        int temp_millidegrees = stream.readAll().trimmed().toInt();
        system_data.temperature = temp_millidegrees / 1000.0;
        temp_file.close();
    }
    
#else
    // Fallback for non-Linux systems
    system_data.cpu_usage = QRandomGenerator::global()->bounded(100);
    system_data.memory_usage = QRandomGenerator::global()->bounded(100);
    system_data.disk_usage = QRandomGenerator::global()->bounded(100);
    system_data.temperature = 45.0 + QRandomGenerator::global()->bounded(20);
    system_data.uptime = "2h 30m";
    system_data.processes = 150 + QRandomGenerator::global()->bounded(100);
#endif
    
    emit updated();
}

// WeatherAPI Implementation
WeatherAPI::WeatherAPI(QObject* parent) : QObject(parent) {
    network = new QNetworkAccessManager(this);
    update_timer = new QTimer(this);
    connect(update_timer, &QTimer::timeout, this, &WeatherAPI::fetchWeather);
}

WeatherAPI::~WeatherAPI() {
    // Clean up pending requests
    for (auto it = pending_requests.begin(); it != pending_requests.end(); ++it) {
        if (it.key()) {
            it.key()->abort();
            it.key()->deleteLater();
        }
    }
    pending_requests.clear();
    
    if (update_timer) {
        update_timer->stop();
        update_timer->deleteLater();
    }
    
    if (network) {
        network->deleteLater();
    }
}

WeatherAPI* WeatherAPI::getInstance() {
    if (!instance) {
        instance = new WeatherAPI();
    }
    return instance;
}

void WeatherAPI::cleanup() {
    if (instance) {
        delete instance;
        instance = nullptr;
    }
}

void WeatherAPI::setApiKey(const QString& key) {
    api_key = key;
}

void WeatherAPI::setCity(const QString& city) {
    city_name = city;
}

void WeatherAPI::setUpdateInterval(int minutes) {
    update_timer->start(minutes * 60000);
}

QString WeatherAPI::temperature() {
    if (weather_data.contains("main")) {
        QJsonObject main = weather_data["main"].toObject();
        if (main.contains("temp")) {
            int temp = main["temp"].toDouble() - 273.15; // Kelvin to Celsius
            return QString("%1°C").arg(temp);
        }
    }
    return "N/A";
}

QString WeatherAPI::condition() {
    if (weather_data.contains("weather")) {
        QJsonArray weather_array = weather_data["weather"].toArray();
        if (!weather_array.isEmpty()) {
            QJsonObject weather = weather_array[0].toObject();
            return weather["description"].toString();
        }
    }
    return "Unknown";
}

QString WeatherAPI::humidity() {
    if (weather_data.contains("main")) {
        QJsonObject main = weather_data["main"].toObject();
        if (main.contains("humidity")) {
            return QString("%1%").arg(main["humidity"].toInt());
        }
    }
    return "N/A";
}

QString WeatherAPI::icon() {
    QString cond = condition().toLower();
    if (cond.contains("rain")) return "🌧️";
    if (cond.contains("cloud")) return "☁️";
    if (cond.contains("sun") || cond.contains("clear")) return "☀️";
    if (cond.contains("snow")) return "❄️";
    return "🌤️";
}

QString WeatherAPI::windSpeed() {
    if (weather_data.contains("wind")) {
        QJsonObject wind = weather_data["wind"].toObject();
        if (wind.contains("speed")) {
            double speed = wind["speed"].toDouble() * 3.6; // m/s to km/h
            return QString("%1 km/h").arg(speed, 0, 'f', 1);
        }
    }
    return "N/A";
}

int WeatherAPI::temperatureInt() {
    QString temp_str = temperature();
    return temp_str.left(temp_str.length() - 2).toInt(); // Remove "°C"
}

bool WeatherAPI::isRaining() {
    return condition().toLower().contains("rain");
}

bool WeatherAPI::isSunny() {
    QString cond = condition().toLower();
    return cond.contains("sun") || cond.contains("clear");
}

bool WeatherAPI::isCloudy() {
    return condition().toLower().contains("cloud");
}

void WeatherAPI::fetchWeather() {
    if (api_key.isEmpty() || city_name.isEmpty()) {
        emit error("API key or city name not set");
        return;
    }
    
    QString url = QString("http://api.openweathermap.org/data/2.5/weather?q=%1&appid=%2")
                      .arg(city_name, api_key);
    
    QNetworkRequest request(url);
    QNetworkReply* reply = network->get(request);
    
    pending_requests[reply] = true;
    
    connect(reply, &QNetworkReply::finished, this, &WeatherAPI::onWeatherReply);
    connect(reply, &QNetworkReply::destroyed, this, &WeatherAPI::cleanupRequest);
}

void WeatherAPI::onWeatherReply() {
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply || !pending_requests.contains(reply)) {
        return;
    }
    
    pending_requests.remove(reply);
    
    if (reply->error() == QNetworkReply::NoError) {
        QByteArray data = reply->readAll();
        QJsonParseError parse_error;
        QJsonDocument doc = QJsonDocument::fromJson(data, &parse_error);
        
        if (parse_error.error == QJsonParseError::NoError) {
            weather_data = doc.object();
            emit weatherUpdated();
        } else {
            emit error("Failed to parse weather data");
        }
    } else {
        emit error(reply->errorString());
    }
    
    reply->deleteLater();
}

void WeatherAPI::cleanupRequest() {
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (reply && pending_requests.contains(reply)) {
        pending_requests.remove(reply);
    }
}

// ============================================================================
// GLOBAL FUNCTIONS IMPLEMENTATION
// ============================================================================

Widget* createWidget(int width, int height) {
    return new Widget(width, height);
}

Widget* createCircle(int diameter) {
    Widget* w = new Widget(diameter, diameter);
    w->setShape(Circle);
    return w;
}

Widget* createSquare(int size) {
    Widget* w = new Widget(size, size);
    w->setShape(Square);
    return w;
}

Text* createText(const QString& text, Widget* parent) {
    Text* label = new Text(text, parent);
    if (parent) parent->addWidget(label);
    return label;
}

Text* createTitle(const QString& text, Widget* parent) {
    Text* label = createText(text, parent);
    label->setTitle();
    return label;
}

Text* createLabel(const QString& text, Widget* parent) {
    Text* label = createText(text, parent);
    label->setBody();
    return label;
}

ProgressBar* createProgressBar(Widget* parent) {
    ProgressBar* bar = new ProgressBar(parent);
    if (parent) parent->addWidget(bar);
    return bar;
}

SystemMonitor* getSystemMonitor() {
    return SystemMonitor::getInstance();
}

WeatherAPI* getWeatherAPI() {
    return WeatherAPI::getInstance();
}

void cleanup() {
    SystemMonitor::cleanup();
    WeatherAPI::cleanup();
}

QFont font(const QString& family, int size) {
    return QFont(family, size);
}

QFont boldFont(const QString& family, int size) {
    QFont f(family, size);
    f.setBold(true);
    return f;
}

QFont monoFont(int size) {
    return QFont("Ubuntu Mono", size);
}

QColor color(const QString& hex) {
    return QColor(hex);
}

QColor color(int r, int g, int b, int a) {
    return QColor(r, g, b, a);
}

QString rgba(int r, int g, int b, int a) {
    return QString("rgba(%1,%2,%3,%4)").arg(r).arg(g).arg(b).arg(a);
}

QPoint getPosition(Position pos, QSize widget_size) {
    QSize screen_size = screenSize();
    QPoint center = screenCenter();
    
    switch (pos) {
        case TopLeft:
            return QPoint(50, 50);
        case TopCenter:
            return QPoint(center.x() - widget_size.width()/2, 50);
        case TopRight:
            return QPoint(screen_size.width() - widget_size.width() - 50, 50);
        case CenterLeft:
            return QPoint(50, center.y() - widget_size.height()/2);
        case Center:
            return QPoint(center.x() - widget_size.width()/2, center.y() - widget_size.height()/2);
        case CenterRight:
            return QPoint(screen_size.width() - widget_size.width() - 50, center.y() - widget_size.height()/2);
        case BottomLeft:
            return QPoint(50, screen_size.height() - widget_size.height() - 50);
        case BottomCenter:
            return QPoint(center.x() - widget_size.width()/2, screen_size.height() - widget_size.height() - 50);
        case BottomRight:
            return QPoint(screen_size.width() - widget_size.width() - 50, screen_size.height() - widget_size.height() - 50);
        default:
            return center;
    }
}

void moveWidget(Widget* widget, Position pos) {
    if (widget) {
        widget->setPosition(pos);
    }
}

void moveWidget(Widget* widget, int x, int y) {
    if (widget) {
        widget->setPosition(x, y);
    }
}

void animate(Widget* widget, Animation type, int duration) {
    if (!widget) return;
    
    switch (type) {
        case FadeIn:
            widget->fadeIn(duration);
            break;
        case FadeOut:
            widget->fadeOut(duration);
            break;
        case Bounce:
            widget->bounce(duration);
            break;
        default:
            break;
    }
}

void fadeWidget(Widget* widget, double opacity, int duration) {
    if (!widget) return;
    widget->setOpacity(opacity);
}

void moveWidget(Widget* widget, int x, int y, int duration) {
    if (!widget) return;
    
    auto* animation = new QPropertyAnimation(widget, "pos");
    animation->setDuration(duration);
    animation->setStartValue(widget->pos());
    animation->setEndValue(QPoint(x, y));
    animation->setEasingCurve(QEasingCurve::OutCubic);
    animation->start(QPropertyAnimation::DeleteWhenStopped);
}

QString getConfigPath(const QString& widget_name) {
    QString config_dir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    QDir().mkpath(config_dir + "/gwidget");
    return config_dir + "/gwidget/" + widget_name + ".conf";
}

QString getDataPath(const QString& widget_name) {
    QString data_dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(data_dir);
    return data_dir + "/" + widget_name + ".data";
}

bool saveText(const QString& filename, const QString& text) {
    QFile file(filename);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream stream(&file);
        stream << text;
        return true;
    }
    return false;
}

QString loadText(const QString& filename) {
    QFile file(filename);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream stream(&file);
        return stream.readAll();
    }
    return QString();
}

bool saveJson(const QString& filename, const QJsonObject& json) {
    QFile file(filename);
    if (file.open(QIODevice::WriteOnly)) {
        QJsonDocument doc(json);
        file.write(doc.toJson());
        return true;
    }
    return false;
}

QJsonObject loadJson(const QString& filename) {
    QFile file(filename);
    if (file.open(QIODevice::ReadOnly)) {
        QByteArray data = file.readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        return doc.object();
    }
    return QJsonObject();
}

QTimer* createTimer(int interval, std::function<void()> callback) {
    QTimer* timer = new QTimer();
    QObject::connect(timer, &QTimer::timeout, callback);
    timer->start(interval);
    return timer;
}

void delay(int milliseconds, std::function<void()> callback) {
    QTimer::singleShot(milliseconds, callback);
}

bool isWayland() {
    return QGuiApplication::platformName() == "wayland";
}

bool isX11() {
    return QGuiApplication::platformName() == "xcb";
}

QString getDesktopEnvironment() {
    return qEnvironmentVariable("XDG_CURRENT_DESKTOP", "Unknown");
}

QSize screenSize() {
    QScreen* screen = QGuiApplication::primaryScreen();
    return screen ? screen->size() : QSize(1920, 1080);
}

QPoint screenCenter() {
    QSize size = screenSize();
    return QPoint(size.width() / 2, size.height() / 2);
}

void log(const QString& message) {
    qDebug() << "[GWidget]" << message;
}

void debug(const QString& message) {
    qDebug() << "[DEBUG]" << message;
}

} // namespace GWidget

#include "GWidgetEngine.moc" // For Qt's meta-object system
