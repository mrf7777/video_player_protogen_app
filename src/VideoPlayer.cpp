#include <protogen/IProtogenApp.hpp>
#include <protogen/IProportionProvider.hpp>
#include <protogen/Resolution.hpp>
#include <protogen/StandardAttributeStore.hpp>
#include <opencv2/opencv.hpp>
#include <cmake_vars.h>

#include <atomic>
#include <cmath>
#include <mutex>
#include <thread>
#include <chrono>

using namespace protogen;

class VideoPlayer : public IProtogenApp {
public:
    VideoPlayer()
        : m_mouthProvider(nullptr),
        m_frame(cv::Mat()),
        m_frameUpdateThread(),
        m_framerate(1.0f),
        m_deviceResolution(1, 1),
        m_active(false),
        m_resourcesDirectory(""),
        m_attributes(std::shared_ptr<StandardAttributeStore>(new StandardAttributeStore()))
    {
        using namespace protogen::attributes;
        using Access = protogen::attributes::IAttributeStore::Access;
        m_attributes->adminSetAttribute(ATTRIBUTE_ID, PROTOGEN_APP_ID, Access::Read);
        m_attributes->adminSetAttribute(ATTRIBUTE_NAME, "Video Test", Access::Read);
        m_attributes->adminSetAttribute(ATTRIBUTE_DESCRIPTION, "Testing video processing on protogen.", Access::Read);
        m_attributes->adminSetAttribute(ATTRIBUTE_THUMBNAIL, "/static/thumbnail.png", Access::Read);
        m_attributes->adminSetAttribute(ATTRIBUTE_MAIN_PAGE, "/static/index.html", Access::Read);
        m_attributes->adminSetAttribute(ATTRIBUTE_HOME_PAGE, "https://github.com/mrf7777/video_player_protogen_app", Access::Read);
    }

    bool sanityCheck([[maybe_unused]] std::string& errorMessage) const override {
        return true;
    }

    void initialize() override {
        m_webServerThread = std::thread([this](){
            using httplib::Request, httplib::Response;
            auto server = httplib::Server();

            // TODO: web interface.
            server.set_mount_point("/static", m_resourcesDirectory + "/static");

            m_webServerPort = server.bind_to_any_port("0.0.0.0");
            server.listen_after_bind();
        });
        m_webServerThread.detach();
    }

    void setActive(bool active) override {
        m_active = active;
        if(m_active) {
            cv::VideoCapture video_capture("/home/mrf777/dev/video_player_protogen_app/build/protogen.mp4");
            if(!video_capture.isOpened()) {
                throw std::runtime_error("Could not open video file. Why was I passed a video capture that is not opened?");
            }
            startFrameUpdateThread(video_capture);
        } else {
            m_frameUpdateThread.join();
        }
    }

    void receiveResourcesDirectory(const std::string& resources_directory) override {
        m_resourcesDirectory = resources_directory;
    }

    void receiveUserDataDirectory([[maybe_unused]] const std::string& user_data_directory) override {
    }

    int webPort() const override {
        return m_webServerPort;
    }

    void render(ICanvas& canvas) const override {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        if(m_frame.empty()) {
            return;
        }

        for(int i = 0; i < m_frame.rows; i++) {
            for(int j = 0; j < m_frame.cols; j++) {
                cv::Vec3b pixel = m_frame.at<cv::Vec3b>(i, j);
                canvas.setPixel(j, i, pixel[0], pixel[1], pixel[2]);
            }
        }
    }

    float framerate() const override {
        return m_framerate;
    }

    void receiveDeviceResolution(const Resolution& device_resolution) override {
        m_deviceResolution = device_resolution;
    }

    std::vector<Resolution> supportedResolutions() const override {
        return {m_deviceResolution};
    }

    void setMouthProportionProvider(std::shared_ptr<IProportionProvider> provider) {
        m_mouthProvider = provider;
    }

private:
    /**
     * @brief Start the frame update thread.
     * 
     * @param video_capture The video capture object to show. Takes ownership of the object.
    */
    void startFrameUpdateThread(cv::VideoCapture video_capture) {
        m_framerate = video_capture.get(cv::VideoCaptureProperties::CAP_PROP_FPS);
        m_frameUpdateThread = std::thread(&VideoPlayer::frameUpdateThread, this, video_capture);
    }

    void frameUpdateThread(cv::VideoCapture video_capture) {
        const double time_interval = 1.0 / m_framerate;
        const auto start_time = std::chrono::high_resolution_clock::now();
        cv::Mat frame;
        int frame_number;
        while(video_capture.read(frame)) {
            if(!m_active) {
                break;
            }
            frame_number = whatFrameShouldIRenderNow(start_time, time_interval);
            const unsigned int device_width = m_deviceResolution.width();
            const unsigned int device_height = m_deviceResolution.height();
            const cv::Size device_size(device_width, device_height);
            cv::resize(frame, frame, device_size);
            cv::cvtColor(frame, frame, cv::COLOR_BGR2RGB);
            {
                std::lock_guard<std::mutex> lock(m_frameMutex);
                m_frame = frame;
            }
            std::this_thread::sleep_until(whenShouldISetNextFrame(start_time, time_interval, frame_number));
        }
    }

    int whatFrameShouldIRenderNow(const std::chrono::time_point<std::chrono::high_resolution_clock>& start_time, double time_interval) {
        const auto now = std::chrono::high_resolution_clock::now();
        const double time_elapsed = std::chrono::duration<double>(now - start_time).count();
        const double section = std::floor(time_elapsed / time_interval);
        return section + 1;
    }

    std::chrono::time_point<std::chrono::high_resolution_clock> whenShouldISetNextFrame(const std::chrono::time_point<std::chrono::high_resolution_clock>& start_time, double time_interval, int current_frame) {
        const auto start_time_of_next_frame = start_time + std::chrono::duration_cast<std::chrono::high_resolution_clock::duration>(std::chrono::duration<double>(time_interval * current_frame));
        return start_time_of_next_frame;
    }

    std::shared_ptr<attributes::IAttributeStore> getAttributeStore() override {
        return m_attributes;
    }

    std::shared_ptr<IProportionProvider> m_mouthProvider;
    mutable std::mutex m_frameMutex;
    cv::Mat m_frame;
    std::thread m_frameUpdateThread;
    float m_framerate;
    Resolution m_deviceResolution;
    std::atomic<bool> m_active;
    std::string m_resourcesDirectory;
    std::shared_ptr<StandardAttributeStore> m_attributes;
    std::thread m_webServerThread;
    int m_webServerPort;
};

// Interface to create and destroy you app.
// This is how your app is created and consumed as a library.
extern "C" IProtogenApp * create_app() {
    return new VideoPlayer();
}

extern "C" void destroy_app(IProtogenApp * app) {
    delete app;
}