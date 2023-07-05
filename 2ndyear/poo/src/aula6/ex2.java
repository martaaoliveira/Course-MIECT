package aula6;

public class ex2 {
    public static void main(String[] args) {
        conjunto c1 = new conjunto();
        c1.insert(4); 
        c1.insert(7); 
        c1.insert(6); 
        c1.insert(5);
        conjunto c2 = new conjunto();
        int[] test = { 7, 3, 2, 5, 4, 6, 7};
        for (int el : test) c2.insert(el);
        c2.remove(3); c2.remove(5); c2.remove(6);
        System.out.println(c1);
        System.out.println(c2);
        System.out.println("Número de elementos em c1: " + c1.tamanho());
        System.out.println("Número de elementos em c2: " + c2.tamanho());
        System.out.println("c1 contém 6?: " + ((c1.contains(6) ? "sim" : "não")));
        System.out.println("c2 contém 6?: " + ((c2.contains(6) ? "sim" : "não")));
        System.out.println("União:" + c1.combine(c2));
        System.out.println("Interseção:" + c1.intersect(c2));
        System.out.println("Diferença:" + c1.subtract(c2));
        c1.empty();
        System.out.println("c1:" + c1);
 }
}

