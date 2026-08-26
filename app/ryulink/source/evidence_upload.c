#include "evidence_upload.h"
#include "app_version.h"
#include "evidence_redaction.h"
#include "localization.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#define L(english, chinese) ryuLinkLocalize((english), (chinese))

enum {
    CoreLogLimit = 256 * 1024,
    AppLogLimit = 64 * 1024,
    CrashReportLimit = 384 * 1024,
    BundleLimit = 1024 * 1024,
    LineBufferSize = 2048,
};

static const char BundlePath[] =
    "sdmc:/config/ryulink/diagnostic_evidence_upload.txt";

typedef struct {
    FILE *output;
    size_t bytes_written;
} BundleWriter;

static bool write_bytes(BundleWriter *writer, const char *data, size_t length) {
    if (writer == NULL || writer->output == NULL || data == NULL ||
        length > BundleLimit - writer->bytes_written) return false;
    if (fwrite(data, 1, length, writer->output) != length) return false;
    writer->bytes_written += length;
    return true;
}

static bool write_text(BundleWriter *writer, const char *text) {
    return write_bytes(writer, text, strlen(text));
}

static bool append_log_tail(BundleWriter *writer, const char *label,
                            const char *path, size_t limit) {
    FILE *input = fopen(path, "rb");
    char header[320];
    char line[LineBufferSize];
    char redacted[LineBufferSize];
    long size;
    long offset;
    size_t available;
    bool copied = false;
    if (input == NULL) return false;
    if (fseek(input, 0, SEEK_END) != 0 || (size = ftell(input)) < 0) {
        fclose(input);
        return false;
    }
    /* Reserve a full header before calculating the tail. This keeps a bundle
     * valid even when every optional source is present. */
    if (writer == NULL || writer->bytes_written > BundleLimit - sizeof(header)) {
        fclose(input);
        return false;
    }
    available = BundleLimit - writer->bytes_written - sizeof(header);
    if (limit > available) limit = available;
    offset = size > (long)limit ? size - (long)limit : 0;
    if (fseek(input, offset, SEEK_SET) != 0) {
        fclose(input);
        return false;
    }
    if (offset > 0) (void)fgets(line, sizeof(line), input);
    if (snprintf(header, sizeof(header),
                 "\n--- %s source_size=%ld tail_offset=%ld ---\n",
                 label, size, offset) >= (int)sizeof(header) ||
        !write_text(writer, header)) {
        fclose(input);
        return false;
    }
    while (fgets(line, sizeof(line), input) != NULL) {
        size_t redacted_length = 0;
        if (!ryuLinkEvidenceRedactLine(line, redacted, sizeof(redacted),
                                       &redacted_length) ||
            !write_bytes(writer, redacted, redacted_length)) break;
        copied = true;
    }
    fclose(input);
    return copied;
}

/* A missing source must be observable in the uploaded evidence. Otherwise a
 * successful upload of the other logs can look like a successful capture of
 * every required subsystem. Keep the normal section header shape so remote
 * readers can list this source alongside present log tails. */
static bool append_missing_source(BundleWriter *writer, const char *label,
                                  const char *status) {
    char section[256];
    if (snprintf(section, sizeof(section),
                 "\n--- %s source_size=0 tail_offset=0 ---\n"
                 "source_status=%s\n",
                 label, status) >= (int)sizeof(section)) {
        return false;
    }
    return write_text(writer, section);
}

static bool latest_crash_report(char *path, size_t path_size) {
    static const char Directory[] = "sdmc:/atmosphere/crash_reports";
    DIR *directory = opendir(Directory);
    struct dirent *entry;
    time_t newest = 0;
    bool found = false;
    if (directory == NULL) return false;
    while ((entry = readdir(directory)) != NULL) {
        char candidate[512];
        struct stat info;
        size_t name_length = strlen(entry->d_name);
        if (entry->d_name[0] == '.') continue;
        if (name_length < 4 || strcasecmp(entry->d_name + name_length - 4, ".log") != 0) continue;
        if (snprintf(candidate, sizeof(candidate), "%s/%s", Directory,
                     entry->d_name) >= (int)sizeof(candidate)) continue;
        if (stat(candidate, &info) != 0 || !S_ISREG(info.st_mode)) continue;
        if (!found || info.st_mtime > newest) {
            if (snprintf(path, path_size, "%s", candidate) >= (int)path_size) continue;
            newest = info.st_mtime;
            found = true;
        }
    }
    closedir(directory);
    return found;
}

static bool build_bundle(RyuLinkEvidenceUpload *upload) {
    BundleWriter writer;
    char crash_path[512];
    mkdir("sdmc:/config/ryulink", 0777);
    memset(&writer, 0, sizeof(writer));
    writer.output = fopen(BundlePath, "wb");
    if (writer.output == NULL) return false;
    if (!write_text(&writer,
                    "RYULINK_DIAGNOSTIC_EVIDENCE_V1\n"
                    "privacy=explicit_user_action,bounded_tails,client_redacted\n"
                    "source_manifest=explicit-missing-v3\n"
                    "app_version=" RYULINK_APP_VERSION "\n")) goto failed;
    if (append_log_tail(&writer, "ldn_mitm",
                        "sdmc:/ldn_mitm.log", CoreLogLimit)) {
        ++upload->files_collected;
    } else if (!append_missing_source(&writer, "ldn_mitm",
                                      "missing_or_unreadable")) {
        goto failed;
    }
    if (append_log_tail(&writer, "app_core_diagnostics",
                        "sdmc:/config/ryulink/core_diagnostics.log",
                        AppLogLimit)) ++upload->files_collected;
    if (latest_crash_report(crash_path, sizeof(crash_path)) &&
        append_log_tail(&writer, "latest_atmosphere_crash_report",
                        crash_path, CrashReportLimit)) ++upload->files_collected;
    if (upload->files_collected == 0 || fflush(writer.output) != 0) goto failed;
    upload->bundle_bytes = (uint32_t)writer.bytes_written;
    fclose(writer.output);
    return true;

failed:
    fclose(writer.output);
    unlink(BundlePath);
    return false;
}

bool ryuLinkEvidenceUploadRun(RyuLinkAuthSession *session,
                              RyuLinkEvidenceUpload *upload) {
    RyuLinkApiEvidenceUpload result;
    if (upload == NULL) return false;
    memset(upload, 0, sizeof(*upload));
    upload->ran = true;
    if (!build_bundle(upload)) {
        snprintf(upload->message, sizeof(upload->message), "%s",
                 L("NO DIAGNOSTIC LOGS FOUND", "没有找到可上传的诊断日志"));
        return false;
    }
    memset(&result, 0, sizeof(result));
    if (!ryuLinkApiUploadEvidence(session, BundlePath, &result)) {
        snprintf(upload->message, sizeof(upload->message), "%s",
                 session && session->message[0] ? session->message :
                 L("UPLOAD FAILED - LOGS REMAIN ON SD", "上传失败，原始日志仍保留在 SD 卡"));
        unlink(BundlePath);
        return false;
    }
    unlink(BundlePath);
    upload->success = true;
    snprintf(upload->evidence_id, sizeof(upload->evidence_id), "%s",
             result.evidence_id);
    snprintf(upload->message, sizeof(upload->message),
             L("UPLOADED %u FILES - EVIDENCE %s", "已上传 %u 个日志 - 证据编号 %s"),
             (unsigned)upload->files_collected, upload->evidence_id);
    return true;
}
