class Student:
    def __init__(self, name, age, scores):
        self.name = name
        self.age = age
        self.scores = scores
    def average(self):
        return sum(self.scores) / len(self.scores)
student1 = Student("zhi", 18, [90, 85, 90])
student2 = Student("zhizhi", 17, [80, 100, 85])
student3 = Student("zhizhizhi", 18, [100, 82,90])
print(f"{student1.name}的平均成绩：{student1.average():.1f}")
print(f"{student2.name}的平均成绩：{student2.average():.1f}")
print(f"{student3.name}的平均成绩：{student3.average():.1f}")