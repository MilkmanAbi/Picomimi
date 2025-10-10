#include <QApplication>
#include <QTimer>
#include "GWidgetEngine.h"

using namespace GWidget;

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Create a main system monitor widget
    Widget* monitor = createWidget(350, 250);
    monitor->setBackground(20, 25, 35, 230);
    monitor->setRounded(12);
    monitor->setPosition(TopRight);
    monitor->setGlass(true);  // Subtle shadow instead of harsh blur

    // Title
    Text* title = createTitle("System Monitor", monitor);
    title->setColor("#4A9EFF");

    // Get system monitor instance (singleton, no memory leaks)
    SystemMonitor* sys = getSystemMonitor();

    // CPU progress bar
    Text* cpu_label = createLabel("CPU Usage", monitor);
    cpu_label->setColor("#CCCCCC");
    ProgressBar* cpu_bar = createProgressBar(monitor);
    cpu_bar->setColors("#333333", "#FF6B6B");

    // Memory progress bar  
    Text* mem_label = createLabel("Memory Usage", monitor);
    mem_label->setColor("#CCCCCC");
    ProgressBar* mem_bar = createProgressBar(monitor);
    mem_bar->setColors("#333333", "#4ECDC4");

    // Info text
    Text* info = createLabel("", monitor);
    info->setMonospace();
    info->setColor("#A8A8A8");

    // Update timer (properly managed)
    QTimer* update_timer = new QTimer(&app);
    QObject::connect(update_timer, &QTimer::timeout, [=]() {
        // Update progress bars
        cpu_bar->setValue(sys->cpu());
        mem_bar->setValue(sys->memory());
        
        // Update info text
        QString info_text = QString("Uptime: %1\nProcesses: %2\nTemp: %3°C")
            .arg(sys->uptime())
            .arg(sys->processes())
            .arg(sys->temperature(), 0, 'f', 1);
        info->setText(info_text);
    });
    update_timer->start(1000);

    // Create a weather widget
    Widget* weather = createWidget(280, 180);
    weather->setBackground(35, 25, 45, 200);
    weather->setRounded(15);
    weather->setPosition(TopLeft);
    weather->setBlur(true, 8.0);  // Fixed blur with configurable radius

    Text* weather_title = createTitle("Weather", weather);
    weather_title->setColor("#FFB74D");

    Text* weather_info = createLabel("Click to set up weather API", weather);
    weather_info->setAlign("center");

    // Demo animation widget
    Widget* demo = createCircle(120);
    demo->setBackground(60, 20, 80, 180);
    demo->setPosition(BottomCenter);
    demo->glow("#FF4081", 15);

    Text* demo_text = createText("Demo", demo);
    demo_text->setAlign("center");
    demo_text->setColor("#FFFFFF");
    demo_text->setBold(true);

    // Animate the demo widget (removed glow pulsing to stop visual "pulse")
    QTimer* anim_timer = new QTimer(&app);
    QObject::connect(anim_timer, &QTimer::timeout, [=]() {
        static int counter = 0;
        switch (counter % 2) {  // Only 2 animations now - removed glow cycling
            case 0: demo->bounce(1200); break; // Slower, gentler bounce
            case 1: demo->fadeOut(1500);  // Much longer fade duration
                    QTimer::singleShot(1600, [=](){ demo->fadeIn(1500); }); 
                    break;
        }
        counter++;
    });
    anim_timer->start(3000);

    // Show all widgets
    monitor->show();
    weather->show(); 
    demo->show();

    // Proper cleanup on exit
    QObject::connect(&app, &QApplication::aboutToQuit, []() {
        cleanup(); // Clean up singletons
    });

    return app.exec();
}
