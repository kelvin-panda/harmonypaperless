#ifndef OHOS_CJSON_COMPAT_H
#define OHOS_CJSON_COMPAT_H

/* Minimal cJSON declarations for the bridge layer.
 * The implementation is provided by the prebuilt libmeetcore.so. */
#ifdef __cplusplus
extern "C" {
#endif

typedef struct cJSON cJSON;

cJSON *cJSON_CreateObject(void);
cJSON *cJSON_CreateArray(void);
cJSON *cJSON_CreateString(const char *string);
cJSON *cJSON_CreateNumber(double num);
void cJSON_AddItemToObject(cJSON *object, const char *string, cJSON *item);
void cJSON_AddItemToArray(cJSON *array, cJSON *item);
void cJSON_AddNumberToObject(cJSON *object, const char *name, double number);
void cJSON_AddStringToObject(cJSON *object, const char *name, const char *string);
int cJSON_GetArraySize(const cJSON *array);
char *cJSON_PrintUnformatted(const cJSON *item);
void cJSON_Delete(cJSON *item);

#ifdef __cplusplus
}
#endif

#endif
