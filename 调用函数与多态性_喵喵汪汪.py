class Animal:
    def sound(self):
        print("小动物叫")
class Dog(Animal):
    def sound(self):
        print("汪汪汪")
class Mouse(Animal):
    def sound(self):
        print("吱吱吱")
def animal_sound(animal):
    animal.sound()
if __name__ == "__main__":
    dog = Dog()
    mouse = Mouse()
    generic_animal = Animal()
    animal_sound(dog)
    animal_sound(mouse)
    animal_sound(generic_animal)