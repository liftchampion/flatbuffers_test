# Flatbuffer tests

# Что тестировалось
Запись и чтение маркет-даты (L3snapshot)

# Расход памяти:
|Количество уровней ордер-бука| Объем памяти |
|-----------------------------|--------------|
|1                            |  200 B       |
|10                           |  1.1 K       |
|100                          |  8.8 K       |
|1000                         |  87  K       |
|10000                        |  860 K       |

## В замерах времени так же замерилась итерация по std::map (в случае энкодинга) и вставка в него (в случае декодинга)

# Скорость энкодинга
Количество уровней ордер-бука. время (us)
1                              2.8
10                             6.0
100                            49.1
1000                           416.7
10000                          3177.0

# Скорость декодинга с использованием встроенного метода верификации
Количество уровней ордер-бука. время (us)
1                              4.3
10                             14.0
100                            55.6
1000                           212.9
10000                          2845.6

# Скорость декодинга без верификации
Количество уровней ордер-бука. время (us)
1                              4.3
10                             11.4
100                            49.5
1000                           169.8
10000                          2253.0


