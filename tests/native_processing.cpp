#include "native_fake_sdk.h"
#include "native_batch_candidate.h"
#include <thread>
#include <unistd.h>

static void check_summary_privacy() {
    FILE* capture = tmpfile();
    assert(capture);
    fflush(stderr);
    int saved = dup(STDERR_FILENO);
    assert(saved >= 0 && dup2(fileno(capture), STDERR_FILENO) >= 0);
    const std::string stream(32, 'f'), adam = "987654321", uri = "skd://itunes.apple.com/private-uri";
    const std::string input = "AUDIO-SENTINEL!!";
    std::string output;
    assert(native_decrypt(stream, adam, uri, input, output));
    assert(native_decrypt(stream, adam, uri, input, output));
    fflush(stderr);
    assert(ftell(capture) == 0);
    native_decrypt_close(stream);
    fflush(stderr);
    assert(dup2(saved, STDERR_FILENO) >= 0);
    close(saved);
    rewind(capture);
    char line[2048];
    assert(fgets(line, sizeof(line), capture));
    std::string summary(line);
    assert(!fgets(line, sizeof(line), capture));
    fclose(capture);
    assert(summary.find("native_stream_summary end=close first={requests=1 bytes=16 completed_bytes=16") != std::string::npos);
    assert(summary.find("reuse={requests=1 bytes=16 completed_bytes=16") != std::string::npos);
    for (const auto& sentinel : {stream, adam, uri, input, std::string("persistent-key")})
        assert(summary.find(sentinel) == std::string::npos);
}

int main() {
    check_summary_privacy();
    Logger::get().configure(LogLevel::Warn, "", 0);
    const std::string uri = "skd://itunes.apple.com/private-uri-sentinel";
    const std::string a(32, 'a'), b(32, 'b'), c(32, 'c');
    std::vector<std::string> blocks = {std::string(16, 'a'), std::string(32, 'b'),
                                       std::string(16, 'c'), std::string(48, 'd')};
    std::vector<std::string> expected, output;
    for (const auto& block : blocks) {
        expected.emplace_back();
        assert(native_decrypt(a, "1", uri, block, expected.back()));
    }
    native_decrypt_close(a);
    using candidate::Status;
    auto run = [&](const std::string& stream, size_t offset, size_t count) {
        return candidate::run(stream, "1", uri, &blocks.at(offset), count, output, cancelled);
    };
    for (size_t i = 0; i < 4; i += 2) {
        for (const auto& stream : {a, b, c}) {
            assert(run(stream, i, 2).status == Status::Complete);
            assert(output == std::vector<std::string>(expected.begin()+i, expected.begin()+i+2));
        }
    }
    // A URI change resets this stream only.
    assert(candidate::run(a, "1", uri + "2", blocks.data(), 4, output, cancelled).status == Status::Complete);
    assert(output == expected);
    native_decrypt_close_all();
    assert(run(a, 0, 0).status == Status::Invalid);
    assert(run(a, 0, 5).status == Status::Invalid);
    blocks[3].resize(candidate::max_bytes);
    assert(run(a, 0, 4).status == Status::Invalid);
    blocks[3].resize(15);
    assert(run(a, 0, 4).status == Status::Invalid);
    assert(output.empty() && live.empty());
    blocks[3] = std::string(48, 'd');

    cancelled = true;
    assert(run(a, 0, 4).attempted == 0 && live.empty());
    cancelled = false;
    cancel_on_block = 2;
    auto r = run(a, 0, 4);
    assert(r.status == Status::Cancelled && r.attempted == 2);
    assert(output == std::vector<std::string>(expected.begin(), expected.begin()+2));
    native_decrypt_close(a);
    native_decrypt_close(a);
    assert(live.empty());
    cancelled = false;
    cancel_on_block = 0;
    fail_on_block = 3;
    r = run(a, 0, 4);
    assert(r.status == Status::Failed && r.attempted == 3 && live.empty());
    assert(output == std::vector<std::string>(expected.begin(), expected.begin()+2));
    fail_on_block = 0;

    std::thread download([&] {
        for (int i = 0; i < 100; ++i) {
            assert(run(a, 0, 4).status == Status::Complete && output == expected);
            native_decrypt_close(a);
        }
    });
    for (int i = 0; i < 100; ++i) {
        for (size_t j = 0; j < blocks.size(); ++j) {
            std::string result;
            assert(native_decrypt(b, "2", uri, blocks[j], result) && result == expected[j]);
        }
        native_decrypt_close(b);
    }
    download.join();
    assert(live.empty());
    puts("native block boundaries, stream isolation, rotation, limits, cancellation, partial failure and contention passed");
}
