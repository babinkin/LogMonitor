# generate_logs.py
import random
from datetime import datetime, timedelta
import os

def generate_log_file(filename, num_entries):
    levels = ['DEBUG', 'INFO', 'WARN', 'ERROR', 'FATAL']
    sources = ['Auth', 'Database', 'Cache', 'API', 'Network', 'System', 'Processor', 'Memory']
    
    messages = {
        'DEBUG': ['Query executed in {}ms', 'Cache hit for key: {}', 'Variable value: {}', 'State changed to {}'],
        'INFO': ['User logged in', 'User logged out', 'Health check passed', 'Connection established', 'Service started'],
        'WARN': ['Cache miss for key: {}', 'Rate limit approaching', 'Memory usage high: {}%', 'Slow response: {}ms'],
        'ERROR': ['Request timeout after {}s', 'Connection pool exhausted', 'Authentication failed', 'Disk write failed'],
        'FATAL': ['System crash detected', 'Out of memory', 'Stack overflow', 'Critical service down']
    }
    
    with open(filename, 'w') as f:
        base_time = datetime(2024, 1, 15, 10, 0, 0)
        
        for i in range(num_entries):
            # Генерируем время с нарастанием
            time_offset = timedelta(seconds=i)
            timestamp = base_time + time_offset
            
            # Выбираем уровень с определенной вероятностью
            rand = random.random()
            if rand < 0.6:
                level = 'INFO'
            elif rand < 0.8:
                level = 'DEBUG'
            elif rand < 0.9:
                level = 'WARN'
            elif rand < 0.97:
                level = 'ERROR'
            else:
                level = 'FATAL'
            
            source = random.choice(sources)
            
            # Генерируем сообщение
            msg_template = random.choice(messages[level])
            if '{}' in msg_template:
                if 'ms' in msg_template:
                    param = random.randint(1, 1000)
                elif 's' in msg_template and 'timeout' in msg_template:
                    param = random.randint(10, 60)
                elif '%' in msg_template:
                    param = random.randint(70, 99)
                elif 'key' in msg_template:
                    param = f"key_{random.randint(1, 10000)}"
                else:
                    param = random.randint(1, 100)
                message = msg_template.format(param)
            else:
                message = msg_template
            
            # Форматируем строку лога
            log_line = f"[{timestamp.strftime('%Y-%m-%d %H:%M:%S')}] [{level}] [{source}] {message}\n"
            f.write(log_line)

if __name__ == "__main__":
    # Генерируем small_log (100 записей)
    generate_log_file("small_log.log", 100)
    print("Generated small_log.log with 100 entries")
    
    # Генерируем large_log (100000 записей)
    generate_log_file("large_log.log", 100000)
    print("Generated large_log.log with 100000 entries")