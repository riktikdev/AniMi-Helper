#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <clocale>
#include <string>
#include <curl/curl.h>
#include <regex>
#include <chrono>
#include <ctime>

#ifdef _WIN32
#include <Windows.h>
#endif

#include <nlohmann/json.hpp>

using namespace std;
using json = nlohmann::json;

const string configFile = "config.json";
json config;

// Макросы цветов
#define COLOR_RESET   "\033[0m"
#define COLOR_BLACK   "\033[0;30m"
#define COLOR_RED     "\033[0;31m"
#define COLOR_GREEN   "\033[0;32m"
#define COLOR_YELLOW  "\033[0;33m"
#define COLOR_BLUE    "\033[0;34m"
#define COLOR_MAGENTA "\033[0;35m"
#define COLOR_CYAN    "\033[0;36m"
#define COLOR_WHITE   "\033[0;37m"

// Структуры
struct Anime {
    int id;
    int shikimoriId;
    int myAnimeListId;
    string name;
    string russian;
    string english;
    int episodes;
    int episodesAired;
    int duration;
    string description;
    vector<string> synonyms;
};

struct User {
    int id;
    string username;
    string globalName;
    bool verified;
    string avatar;
    string createdAt;
    string updatedAt;
};

/**
 * @brief Проверяет, что программа запущена на операционной системе Windows.
 *
 * Если программа не запущена на Windows, выводит сообщение о несовместимости и завершает выполнение.
 */
void check_system() {
#ifndef _WIN32
    cout << "Эта программа может быть запущена только на операционной системе Windows." << endl;
    exit(0);
#endif
}

/**
 * @brief Устанавливает кодировку консоли Windows в 1251 для корректного отображения русских символов.
 *
 * Данная функция использует функции из Windows API для установки кодировки ввода и вывода консоли.
 */
void set_encoding() {
#ifdef _WIN32
    SetConsoleCP(1251);
    SetConsoleOutputCP(1251);
#endif
}

/**
 * @brief Загружает конфигурацию из файла config.json или создает новый файл с значениями по умолчанию, если файл отсутствует.
 */
void load_config() {
    // Проверяем наличие файла config.json
    ifstream file(configFile);
    if (!file.good()) {
        // Файл не найден, создаем новый с значениями по умолчанию
        config["app_name"] = "AniMi Helper";
        config["app_version"] = "1.1";
        config["debug"] = false;
        config["developer"] = "riktikdev";

        ofstream newFile(configFile);
        newFile << setw(4) << config << endl;
        newFile.close();
    }
    else {
        // Загружаем конфигурацию из файла
        file >> config;
        file.close();
    }
}

/**
 * @brief Выводит информационное сообщение в консоль.
 *
 * @param message Сообщение для вывода.
 */
void log_info(const string& message) {
    cout << "[DEBUG] " << message << endl;
}

/**
 * @brief Выводит сообщение об успешном выполнении операции в консоль.
 *
 * @param message Сообщение для вывода.
 */
void log_success(const string& message) {
    cout << COLOR_GREEN << "[DEBUG] " << message << COLOR_RESET << endl;
}

/**
 * @brief Выводит предупреждение в консоль.
 *
 * @param message Сообщение для вывода.
 */
void log_warning(const string& message) {
    cout << COLOR_YELLOW << "[DEBUG] " << message << COLOR_RESET << endl;
}

/**
 * @brief Выводит сообщение об ошибке в консоль.
 *
 * @param message Сообщение для вывода.
 */
void log_error(const string& message) {
    cout << COLOR_RED << "[DEBUG] " << message << COLOR_RESET << endl;
}

/**
 * @brief Инициализирует настройки приложения на основе загруженной конфигурации.
 *
 * Устанавливает заголовок консоли (если поддерживается) и другие настройки из config.
 */
void load_settings() {
#ifdef _WIN32
    if (config["debug"] == true) {
        log_info("Попытка установить заголовок консоли");
    }

    // Устанавливаем заголовок консоли
    string title = config["app_name"].get<string>() + " v" + config["app_version"].get<string>();
    bool success = SetConsoleTitleA(title.c_str());

    if (config["debug"] == true) {
        if (success) {
            log_success("Заголовок консоли успешно установлен");
        }
        else {
            log_error("Не удалось установить заголовок консоли");
        }
    }
#endif
}

/**
 * @brief Очищает консоли
 */
void clear_console() {
    int result;

#ifdef _WIN32
    result = system("cls");
#else
    result = system("clear");
#endif

    if (result != 0 && config["debug"]) {
        log_error("Ошибка при очистке консоли");
    }
}

/**
* @brief Функция обратного вызова для записи данных curl
*/
size_t write_callback(void* contents, size_t size, size_t nmemb, string* userp) {
    size_t realsize = size * nmemb;
    userp->append((char*)contents, realsize);
    return size * nmemb;
}

/**
 * @brief Преобразует дату и время из формата ISO 8601 в строку с форматированным выводом.
 *
 * @param isoDate Строка с датой и временем в формате ISO 8601 (например, "2024-05-31T23:10:39.588Z").
 * @return Строка с отформатированной датой в виде "дд мм. гггг" или "Invalid Date", если входная строка некорректна.
 */
string format_iso_date(const string& isoDate) {
    // Преобразование ISO 8601 строки в структуру времени
    tm time = {};
    stringstream ss(isoDate);
    ss >> get_time(&time, "%Y-%m-%dT%H:%M:%S");

    if (ss.fail()) {
        return "Недопустимая дата";
    }

    // Форматирование вывода даты в "дд мм. гггг"
    stringstream formattedDate;
    formattedDate << put_time(&time, "%d %b. %Y");
    return formattedDate.str();
}

/**
 * @brief Выполняет HTTP GET-запрос по указанному URL.
 *
 * Функция инициализирует CURL, устанавливает URL для запроса GET,
 * назначает функцию обратного вызова для записи полученных данных в строку,
 * выполняет запрос и возвращает полученную строку.
 *
 * @param url URL-адрес для выполнения GET-запроса.
 * @return Строка с данными, полученными в результате GET-запроса.
 */
string http_get_request(const string& url) {
    CURL* curl;
    CURLcode res;
    string readBuffer;

    curl = curl_easy_init();
    if (curl) {
        // Устанавливаем URL для запроса
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

        // Назначаем функцию обратного вызова для записи полученных данных в строку
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

        // Выполняем запрос
        res = curl_easy_perform(curl);

        // Проверяем результат выполнения запроса
        if (res != CURLE_OK && config["debug"] == true) {
            cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << endl;
        }

        // Освобождаем ресурсы curl
        curl_easy_cleanup(curl);
    }

    return readBuffer;
}

/**
 * @brief Выполняет HTTP POST-запрос по указанному URL.
 *
 * Функция инициализирует CURL, устанавливает URL для запроса POST,
 * назначает функцию обратного вызова для записи полученных данных в строку,
 * выполняет запрос и возвращает полученную строку.
 *
 * @param url URL-адрес для выполнения POST-запроса.
 * @param body Данные передаваемые в запрос
 * @return Строка с данными, полученными в результате POST-запроса.
 */
string http_post_request(const string& url, const string& query) {
    CURL* curl;
    CURLcode res;
    string readBuffer;

    curl = curl_easy_init();
    if (curl) {
        const string data = "{\"query\": \"" + query + "\", \"take\": 5}";

        // Устанавливаем URL для запроса
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

        // Устанавливаем метод POST
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        // Устанавливаем передаваемые даанные
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, data.length());

        // Устанавливаем обратный вызов записи для обработки ответа
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

        // Устанавливаем HTTP Headers
        struct curl_slist* headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        res = curl_easy_perform(curl);
        if (res != CURLE_OK && config["debug"] == true) {
            log_error("При отправке запроса произошла ошибка");
        }
        else if (config["debug"] == true) {
            log_success("Запрос успешно отправлен");
        }

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }

    return readBuffer;
}

//string http_post_request(const string& url, const json& body) {
//    CURL* curl;
//    CURLcode res;
//    string readBuffer;
//
//    curl = curl_easy_init();
//    if (curl) {
//        // Устанавливаем URL для запроса
//        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
//
//        // Устанавливаем метод POST
//        curl_easy_setopt(curl, CURLOPT_POST, 1L);
//
//        // Устанавливаем HTTP Headers
//        struct curl_slist* headers = NULL;
//        headers = curl_slist_append(headers, "Content-Type: application/json");
//        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
//
//        // Устанавливаем передаваемые в Body JSON-даанные
//        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.dump().c_str());
//
//        ofstream outFile("response.json");
//        if (!outFile.is_open()) {
//            cerr << "Unable to open file for writing." << endl;
//            return readBuffer; // Return empty string or handle error as per your logic
//        }
//
//        // Устанавливаем обратный вызов записи для обработки ответа
//        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
//        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &outFile);
//
//        // Perform the request
//        res = curl_easy_perform(curl);
//
//        // Check for errors
//        if (res != CURLE_OK && config["debug"] == true) {
//            cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << endl;
//        }
//
//        // Clean up
//        curl_easy_cleanup(curl);
//        curl_slist_free_all(headers);
//
//        // Close the file stream
//        outFile.close();
//    }
//
//    return readBuffer;
//}

/**
 * @brief Выполняет запрос на случайное аниме через API и выводит информацию на консоль.
 *
 * Функция отправляет HTTP GET запрос на указанный URL для получения данных случайного аниме.
 * Полученные данные парсятся в формате JSON, а затем извлекаются и выводятся основные характеристики
 * аниме, такие как ID, названия на разных языках, количество эпизодов и другие атрибуты.
 *
 * Если запрос не вернул данных (пустой ответ), функция рекурсивно вызывает сама себя.
 * Если в полученных данных присутствует ошибка (например, отсутствует запрашиваемое поле), выводится сообщение об ошибке.
 *
 * После вывода информации о случайном аниме функция запрашивает пользователя о желании продолжить.
 * В зависимости от ответа ('y' или 'Y' для продолжения, любой другой ответ для завершения) функция либо
 * рекурсивно вызывает себя для получения нового случайного аниме, либо завершает выполнение программы.
 */
void get_random_anime() {
    // Очищаем консоль
    clear_console();

    string url = "https://api.animi.club/anime/random";
    string response = http_get_request(url);

    if (response.empty()) {
        get_random_anime();
    }

    try {
        json data = json::parse(response);

        // Проверяем на наличие ошибок
        if (data.find("error") != data.end()) {
            string errorMessage = data["error"];
            cout << "[" << COLOR_MAGENTA << "!" << COLOR_RESET << "] "
                << "Ошибка при получении информации о пользователе: " << errorMessage << endl;
            return;
        }

        // Создаем объект аниме и заполяем его данными
        Anime anime;
        anime.id = data["id"];
        anime.shikimoriId = data["shikimoriId"];
        anime.myAnimeListId = data["myAnimeListId"];

        // Проверяем и заполняем поля, если они присутствуют в JSON и не являются null
        if (!data["name"].is_null() && data["name"].is_string()) {
            anime.name = data["name"];
        }
        else {
            if (config["debug"] == true) {
                log_info("Значение 'name' пусто или не является строкой");
            }
            anime.name = "Нет";
        }
        if (!data["russian"].is_null() && data["russian"].is_string()) {
            anime.russian = data["russian"];
        }
        else {
            if (config["debug"] == true) {
                log_info("Значение 'russian' пусто или не является строкой");
            }
            anime.russian = "Нет";
        }
        if (!data["english"].is_null() && data["english"].is_string()) {
            anime.english = data["english"];
        }
        else {
            if (config["debug"] == true) {
                log_info("Значение 'english' пусто или не является строкой");
            }
            anime.english = "Нет";
        }
        if (!data["synonyms"].empty() && data["synonyms"].is_array()) {
            anime.synonyms = data["synonyms"].get<vector<string>>();
        }
        else {
            if (config["debug"] == true) {
                log_info("Значение 'synonyms' пусто или не массивом");
            }
            anime.synonyms = { "Нет" };
        }
        if (data["episodes"].is_number()) {
            anime.episodes = data["episodes"];
        }
        else {
            if (config["debug"] == true) {
                log_info("Значение 'episodes' не является числом");
            }
            anime.episodes = 0;
        }
        if (data["episodesAired"].is_number()) {
            anime.episodesAired = data["episodesAired"];
        }
        else {
            if (config["debug"] == true) {
                log_info("Значение 'episodesAired' не является числом");
            }
            anime.episodesAired = 0;
        }
        if (data["duration"].is_number()) {
            anime.duration = data["duration"];
        }
        else {
            if (config["debug"] == true) {
                log_info("Значение 'duration' не является числом");
            }
            anime.duration = 0;
        }
        if (!data["description"].is_null() && data["description"].is_string()) {
            anime.description = data["description"];
        }
        else {
            if (config["debug"] == true) {
                log_info("Значение 'description' пусто или не является строкой");
            }
            anime.description = "Нет";
        }

        // Выводим информацию о аниме
        cout << "[" << COLOR_MAGENTA << "+" << COLOR_RESET << "] "
            << "Информация о аниме" << endl;
        cout << "ID: " << anime.id << endl;
        cout << "Shikimori ID: " << anime.shikimoriId << endl;
        cout << "MyAnimeList ID: " << anime.myAnimeListId << endl;
        cout << "Название: " << anime.name << endl;
        cout << "Название на русском: " << anime.russian << endl;
        cout << "Название на английском: " << anime.english << endl;

        cout << "Альтернативные названия: ";
        if (anime.synonyms.size() == 0) {
            cout << "Нет";
        }
        else if (anime.synonyms.size() == 1) {
            cout << anime.synonyms[0];
        }
        else {
            for (size_t i = 0; i < anime.synonyms.size(); i++) {
                cout << anime.synonyms[i];
                if (i != anime.synonyms.size() - 1) {
                    cout << ", ";
                }
            }
        }
        cout << endl;

        cout << "Кол-во эпизодов: " << anime.episodes << " / " << anime.episodesAired << endl;
        cout << "Длительность: " << anime.duration << " м." << endl;
        cout << "Описание: " << anime.description << '\n' << endl;

        cout << "Ссылки: " << endl;
        cout << "AniMi Club: " << "https://animi.club/anime/" << anime.id << endl;
        cout << "Shikimori: " << "https://shikimori.one/animes/" << anime.shikimoriId << endl;
        cout << "MyAnimeList: " << "https://myanimelist.net/anime/" << anime.myAnimeListId << '\n' << endl;
    }
    catch (const json::exception& e) {
        cout << "[" << COLOR_MAGENTA << "!" << COLOR_RESET << "] "
            << "Ошибка при обработке данных аниме: " << e.what() << endl;
    }

    string answer;
    cout << "[" << COLOR_MAGENTA << "?" << COLOR_RESET << "] " << "Хотите продолжить? (y/n): ";
    cin >> answer;

    if (answer == "y" || answer == "Y") {
        get_random_anime();
    }
    else {
        exit(0);
    }
}

void get_anime_by_query() {
    clear_console();

    string query;

    cout << "[" << COLOR_MAGENTA << "?" << COLOR_RESET << "] " << "Введите название: ";
    cin >> query;

    clear_console();

    string url = "https://api.animi.club/anime/search";
    string response = http_post_request(url, query);

    try {
        json data = json::parse(response);

        if (data.is_array()) {
            vector<Anime> anime_results;

            for (const auto& anime : data) {
                Anime anime_info;

                anime_info.id = anime["id"];
                anime_info.shikimoriId = anime["shikimoriId"];
                anime_info.myAnimeListId = anime["myAnimeListId"];
                if (!anime["name"].is_null() && anime["name"].is_string()) {
                    anime_info.name = anime["name"];
                }
                else {
                    if (config["debug"] == true) {
                        log_info("Значение 'name' пусто или не является строкой");
                    }
                    anime_info.name = "Нет";
                }
                if (!anime["russian"].is_null() && anime["russian"].is_string()) {
                    anime_info.russian = anime["russian"];
                }
                else {
                    if (config["debug"] == true) {
                        log_info("Значение 'russian' пусто или не является строкой");
                    }
                    anime_info.russian = "Нет";
                }
                if (!anime["english"].is_null() && anime["english"].is_string()) {
                    anime_info.english = anime["english"];
                }
                else {
                    if (config["debug"] == true) {
                        log_info("Значение 'english' пусто или не является строкой");
                    }
                    anime_info.english = "Нет";
                }
                if (anime["episodes"].is_number()) {
                    anime_info.episodes = anime["episodes"];
                }
                else {
                    if (config["debug"] == true) {
                        log_info("Значение 'episodes' не является числом");
                    }
                    anime_info.episodes = 0;
                }
                if (anime["episodesAired"].is_number()) {
                    anime_info.episodesAired = anime["episodesAired"];
                }
                else {
                    if (config["debug"] == true) {
                        log_info("Значение 'episodesAired' не является числом");
                    }
                    anime_info.episodesAired = 0;
                }
                if (anime["duration"].is_number()) {
                    anime_info.duration = anime["duration"];
                }
                else {
                    if (config["debug"] == true) {
                        log_info("Значение 'duration' не является числом");
                    }
                    anime_info.duration = 0;
                }

                anime_results.push_back(anime_info);
            }

            for (const auto& anime : anime_results) {
                cout << "ID: " << anime.id << endl;
                cout << "Shikimori ID: " << anime.shikimoriId << endl;
                cout << "MyAnimeList ID: " << anime.myAnimeListId << endl;
                cout << "Название: " << anime.name << endl;
                cout << "Название на русском: " << anime.russian << endl;
                cout << "Название на английском: " << anime.english << endl;
                cout << "Кол-во эпизодов: " << anime.episodes << " / " << anime.episodesAired << endl;
                cout << "Длительность: " << anime.duration << " м." << endl;
                cout << "Смотреть: " << "https://animi.club/anime/" << anime.id << "\n" << endl;
            }
        }
        else if (data.is_object()) {
            Anime anime_info;

            anime_info.id = data["id"];
            anime_info.shikimoriId = data["shikimoriId"];
            anime_info.myAnimeListId = data["myAnimeListId"];
            if (!data["name"].is_null() && data["name"].is_string()) {
                anime_info.name = data["name"];
            }
            else {
                if (config["debug"] == true) {
                    log_info("Значение 'name' пусто или не является строкой");
                }
                anime_info.name = "Нет";
            }
            if (!data["russian"].is_null() && data["russian"].is_string()) {
                anime_info.russian = data["russian"];
            }
            else {
                if (config["debug"] == true) {
                    log_info("Значение 'russian' пусто или не является строкой");
                }
                anime_info.russian = "Нет";
            }
            if (!data["english"].is_null() && data["english"].is_string()) {
                anime_info.english = data["english"];
            }
            else {
                if (config["debug"] == true) {
                    log_info("Значение 'english' пусто или не является строкой");
                }
                anime_info.english = "Нет";
            }
            if (data["episodes"].is_number()) {
                anime_info.episodes = data["episodes"];
            }
            else {
                if (config["debug"] == true) {
                    log_info("Значение 'episodes' не является числом");
                }
                anime_info.episodes = 0;
            }
            if (data["episodesAired"].is_number()) {
                anime_info.episodesAired = data["episodesAired"];
            }
            else {
                if (config["debug"] == true) {
                    log_info("Значение 'episodesAired' не является числом");
                }
                anime_info.episodesAired = 0;
            }
            if (data["duration"].is_number()) {
                anime_info.duration = data["duration"];
            }
            else {
                if (config["debug"] == true) {
                    log_info("Значение 'duration' не является числом");
                }
                anime_info.duration = 0;
            }

            cout << "ID: " << anime_info.id << endl;
            cout << "Shikimori ID: " << anime_info.shikimoriId << endl;
            cout << "MyAnimeList ID: " << anime_info.myAnimeListId << endl;
            cout << "Название: " << anime_info.name << endl;
            cout << "Название на русском: " << anime_info.russian << endl;
            cout << "Название на английском: " << anime_info.english << endl;
            cout << "Кол-во эпизодов: " << anime_info.episodes << " / " << anime_info.episodesAired << endl;
            cout << "Длительность: " << anime_info.duration << " м." << endl;
            cout << "Смотреть: " << "https://animi.club/anime/" << anime_info.id << "\n" << endl;
        }
        else {
            cout << "[" << COLOR_MAGENTA << "!" << COLOR_RESET << "] "
                << "Ошибка: Ожидался массив или объект данных аниме, получен: " << data.type_name() << endl;
        }
    }
    catch (const json::exception& e) {
        cout << "[" << COLOR_MAGENTA << "!" << COLOR_RESET << "] "
            << "Ошибка при обработке данных аниме: " << e.what() << endl;
    }

    string answer;
    cout << "[" << COLOR_MAGENTA << "?" << COLOR_RESET << "] " << "Хотите продолжить? (y/n): ";
    cin >> answer;

    if (answer == "y" || answer == "Y") {
        get_anime_by_query();
    }
    else {
        exit(0);
    }
}

/**
 * @brief Очищает введенное имя пользователя от недопустимых символов.
 *
 * Функция принимает строку `username` и удаляет из нее все символы, кроме букв (верхнего и нижнего регистра), цифр, символов '-' и '_'. Очищенная строка возвращается в качестве результата.
 *
 * @param username Строка с именем пользователя, которую необходимо очистить.
 * @return Очищенная строка с именем пользователя.
 */
string sanitize_username(string username) {
    // Регулярное выражение для разрешенных символов: буквы, цифры, '-', '_'
    regex pattern("[^a-zA-Z0-9-_]");

    // Заменяем недопустимые символы на пустую строку
    username = regex_replace(username, pattern, "");

    return username;
}

/**
 * @brief Запрашивает у пользователя имя пользователя и очищает его от недопустимых символов.
 *
 * Функция запрашивает у пользователя имя пользователя с помощью стандартного ввода. Затем производит очистку введенного имени от всех символов, кроме букв (верхнего и нижнего регистра), цифр, символов '-' и '_'. Если очищенное имя пользователя не содержит недопустимых символов, оно выводится в консоль для демонстрации.
 */
void get_user_by_username() {
    // Очищаем консоль
    clear_console();

    string username;

    // Спрашиваем имя пользователя
    cout << "[" << COLOR_MAGENTA << "?" << COLOR_RESET << "] " << "Введите имя пользователя: ";
    cin >> username;

    // Очищаем имя пользователя от недопустимых символов
    string sanitized_username = sanitize_username(username);

    // Проверяем, осталось ли что-то от имени пользователя после очистки
    if (sanitized_username.empty()) {
        cout << "[" << COLOR_MAGENTA << "!" << COLOR_RESET << "] "
            << "Имя пользователя содержит недопустимые символы. Разрешены только буквы, цифры, '-', '_'" << endl;
        return;
    }

    clear_console();

    string url = "https://api.animi.club/users/" + sanitized_username;
    string response = http_get_request(url);

    // Проверяем на пустой ответ или внутреннюю серверную ошибку
    if (response.empty()) {
        cout << "[" << COLOR_MAGENTA << "!" << COLOR_RESET << "] "
            << "Пользователь с именем '" << sanitized_username << "' не найден." << endl;
        return;
    }

    // Пытаемся распарсить JSON ответ
    try {
        json data = json::parse(response);

        // Проверяем на наличие ошибок
        if (data.find("error") != data.end()) {
            string errorMessage = data["error"];
            cout << "[" << COLOR_MAGENTA << "!" << COLOR_RESET << "] "
                << "Ошибка при получении информации о пользователе: " << errorMessage << endl;
            return;
        }

        // Создаем объект пользователя и заполняем его данными
        User user;
        user.id = data["id"];
        user.username = sanitized_username;

        // Проверяем и заполняем поля, если они присутствуют в JSON и не являются null
        if (!data["globalName"].is_null()) {
            user.globalName = data["globalName"];
        }
        else {
            user.globalName = "Нет";
        }
        if (!data["avatar"].is_null()) {
            user.avatar = data["avatar"];
        }
        else {
            user.avatar = "Нет";
        }
        if (!data["verified"].is_null()) {
            user.verified = data["verified"];
        }
        else {
            user.verified = false;
        }
        if (!data["createdAt"].is_null()) {
            user.createdAt = format_iso_date(data["createdAt"]);
        }
        else {
            user.createdAt = "Нет данных";
        }
        if (!data["updatedAt"].is_null()) {
            user.updatedAt = format_iso_date(data["updatedAt"]);
        }
        else {
            user.updatedAt = "Нет данных";
        }

        // Выводим информацию о пользователе
        cout << "[" << COLOR_MAGENTA << "+" << COLOR_RESET << "] "
            << "Информация о пользователе '" << sanitized_username << "':" << endl;
        cout << "ID: " << user.id << endl;
        cout << "Имя пользователя: " << user.username << endl;
        cout << "Отображаемое имя: " << user.globalName << endl;
        cout << "Верифицирован: " << (user.verified ? "Да" : "Нет") << endl;
        cout << "Аватар: " << "https://animi-s3.s3.aeza.cloud/" + user.avatar << endl;
        cout << "Дата создания: " << user.createdAt << endl;
        cout << "Дата обновления: " << user.updatedAt << '\n' << endl;
    }
    catch (const json::exception& e) {
        cout << "[" << COLOR_MAGENTA << "!" << COLOR_RESET << "] "
            << "Ошибка при обработке данных пользователя: " << e.what() << endl;
    }

    string answer;
    cout << "[" << COLOR_MAGENTA << "?" << COLOR_RESET << "] " << "Хотите продолжить? (y/n): ";
    cin >> answer;

    if (answer == "y" || answer == "Y") {
        get_user_by_username();
    }
    else {
        exit(0);
    }
}


/**
* @brief Функция для вывода в консоль "Об программе"
*/
void about() {
    clear_console();

    cout << "[" << COLOR_MAGENTA << "?" << COLOR_RESET << "] " << "Что делает программа?" << '\n' << endl;
    cout << "[" << COLOR_MAGENTA << "#" << COLOR_RESET << "] " << "Да х.. его знает, эта программа была написана ради прикола ну и для финального проекта. Этот софт я старался писать так чтобы Денис не говорил где комментарии и че за говно код, мне даже пришлось учить libcurl для подачи HTTP запросов на мой REST-API сервер сайта. Надеюсь Денис это тебе понравиться." << '\n' << endl;

    cout << "[" << COLOR_MAGENTA << "?" << COLOR_RESET << "] " << "Что использовалось?" << '\n' << endl;
    cout << "[" << COLOR_MAGENTA << "#" << COLOR_RESET << "] " << "ChatGPT вообще не использовал, в ютубе прилшось смотреть как сделать хорошее меню, попадались именно видео по созданию консольных меню для читов CS:GO))) Денис не осуждай, это все не я я не контрибьютю читы на геншин :D https://korepi.com. А так использовались такие библиотеки как: fstream - работа с файловой системой; clocale - для установи языка (я прочитал что locale это для Си а не для С++); Windows.h - для использования Windows API; nlohman/json - для работы с JSON форматом; curl - для подачи HTTP запросов на REST-API сервер." << '\n' << endl;

    cout << "[" << COLOR_MAGENTA << "?" << COLOR_RESET << "] " << "Где смотреть аниме?" << '\n' << endl;
    cout << "[" << COLOR_MAGENTA << "#" << COLOR_RESET << "] " << "Вот: https://animi.club (скоро обнова, Миша когда на бек ко мне?)" << '\n' << endl;

    
    cout << "app_name: " << config["app_name"] << endl;
    cout << "app_version: " << config["app_version"] << endl;
    cout << "[" << COLOR_MAGENTA << "#" << COLOR_RESET << "] " << "Отладочная информация" << '\n' << endl;cout << "debug: " << config["debug"] << endl;
    cout << "developer: " << "https://github.com/" + config["developer"].get<string>() << endl;
}

void secret() {
    cout << "Ну что сказать... я ничего лучше не придумал как рубануть питание монитора :D" << endl;
    Sleep(3000);
    cout << "Выключаем через 3.." << endl;
    Sleep(1000);
    cout << "Выключаем через 2.." << endl;
    Sleep(1000);
    cout << "Выключаем через 1.." << endl;
    Sleep(1500);
    SendMessage(HWND_BROADCAST,WM_SYSCOMMAND,SC_MONITORPOWER, (LPARAM)2);
}

void show_menu() {
    clear_console();

    cout << "[" << COLOR_MAGENTA << "#" << COLOR_RESET << "] " << "Выберите один из пунктов меню" << '\n' << endl;

    cout << "[" << COLOR_MAGENTA << "1" << COLOR_RESET << "] " << "Случайное аниме" << endl;
    cout << "[" << COLOR_MAGENTA << "2" << COLOR_RESET << "] " << "Поиск аниме по названию" << endl;
    cout << "[" << COLOR_MAGENTA << "3" << COLOR_RESET << "] " << "Поиск пользователя по названию" << endl;
    cout << "[" << COLOR_MAGENTA << "4" << COLOR_RESET << "] " << "Об программе" << endl;
    cout << "[" << COLOR_MAGENTA << "0" << COLOR_RESET << "] " << "Выход" << '\n' << endl;

    while (true) {
        string input;
        cout << "[" << COLOR_MAGENTA << "?" << COLOR_RESET << "] " << "> ";
        cin >> input;

        bool isNumber = true;
        for (char c : input) {
            if (!isdigit(c)) {
                isNumber = false;
                break;
            }
        }

        if (isNumber) {
            // Преобразуем введенную строку в число
            int choice = stoi(input);

            switch (choice) {
            case 1: {
                get_random_anime();
                break;
            }
            case 2:
                get_anime_by_query();
                break;
            case 3:
                get_user_by_username();
                break;
            case 4:
                about();
                break;
            case 0:
                exit(0);
            default:
                cout << "[" << COLOR_MAGENTA << "!" << COLOR_RESET << "]" << " "
                    << "Данного пункта меню не существует" << endl;
                break;
            }
        }
        else {
            string lower = input;

            // Преобразуем строку в нижний регистр
            for (char& c : lower) {
                c = tolower(c);
            }

            if (lower == "secret") {
                secret();
            }
            else {
                clear_console();
                show_menu();
                cout << "[" << COLOR_MAGENTA << "!" << COLOR_RESET << "]" << " "
                    << "Только цифры разрешены" << endl;
            }
        }
    }  
}

int main() {
    // Устанавливаем русский язык
    setlocale(LC_ALL, "rus");
    // Проверяем тип системы (программа запускается только на Windows)
    check_system();
    // Устанавливаем кодировку Windows 1251 для консоли
    set_encoding();
    // Загружаем или создаем конфигурацию
    load_config();
    // Инициализациянастроек
    load_settings();

    // Инициализация меню
    show_menu();

    return 0;
}
